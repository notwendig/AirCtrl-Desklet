#!/usr/bin/env bash
set -Eeuo pipefail

version="1.05"
tag="v${version}"
commit_message="v1.05: central AirControl server and local clients"
expected_remote="git@github.com:notwendig/AirCtrl-Desklet.git"
source_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
project_dir="${AIRCTRL_PROJECT_DIR:-${HOME}/Projects/Qt/AirCtrl-Desklet}"

fail() {
    printf 'FEHLER: %s\n' "$*" >&2
    exit 1
}

for command_name in git python3 cmake ctest; do
    command -v "$command_name" >/dev/null || fail "$command_name fehlt."
done
[[ -d "$project_dir/.git" ]] || fail "Kein Git-Original gefunden: $project_dir"

project_dir="$(cd -- "$project_dir" && pwd -P)"
git_root="$(git -C "$project_dir" rev-parse --show-toplevel)"
[[ "$git_root" == "$project_dir" ]] ||
    fail "Das Ziel ist nicht die Wurzel des Git-Repositorys: $project_dir"
branch="$(git -C "$project_dir" symbolic-ref --quiet --short HEAD)" ||
    fail "Detached HEAD; bitte zuerst einen Branch auschecken."
[[ -n "$(git -C "$project_dir" config --get user.name || true)" ]] ||
    fail "Git-Benutzername fehlt."
[[ -n "$(git -C "$project_dir" config --get user.email || true)" ]] ||
    fail "Git-E-Mail-Adresse fehlt."
if [[ -n "$(git -C "$project_dir" status --porcelain --untracked-files=all)" ]]; then
    # Ein erster v1.05-Lauf kann nach dem Kopieren, aber vor Commit/Tag/Push
    # am früheren 90-s-CTest-Limit geendet haben. Nur exakt bekannte Dateien
    # dieses abgebrochenen Laufs dürfen automatisch fortgesetzt werden.
    if ! python3 "$source_dir/scripts/check_v105_resume.py" "$source_dir" "$project_dir" \
            "$source_dir/scripts/v1.05-resume.json"; then
        git -C "$project_dir" status --short >&2
        fail "Das Original enthält eigene oder unbekannte Änderungen. Nichts wurde überschrieben."
    fi
fi

if git -C "$project_dir" remote get-url origin >/dev/null 2>&1; then
    git -C "$project_dir" remote set-url origin "$expected_remote"
else
    git -C "$project_dir" remote add origin "$expected_remote"
fi

# Erst synchronisieren, dann Dateien kopieren. Damit wird keine alte, früher
# wegen Mitschnitten bereinigte Historie erneut auf GitHub abgespielt.
git -C "$project_dir" fetch origin \
    "refs/heads/$branch:refs/remotes/origin/$branch"
local_head="$(git -C "$project_dir" rev-parse HEAD)"
remote_head="$(git -C "$project_dir" rev-parse "origin/$branch")"
resume_committed=0
if [[ "$local_head" != "$remote_head" ]]; then
    read -r ahead behind < <(git -C "$project_dir" rev-list --left-right --count \
        "$branch...origin/$branch")
    local_subject="$(git -C "$project_dir" log -1 --format=%s)"
    local_parent="$(git -C "$project_dir" rev-parse HEAD^ 2>/dev/null || true)"
    known_v105_subject=0
    case "$local_subject" in
        "$commit_message"|"v1.05"|"v1.05: central AirControl server and and local clients")
            known_v105_subject=1 ;;
    esac
    if [[ "$ahead" == 1 && "$behind" == 0 && \
          "$known_v105_subject" == 1 && \
          "$local_parent" == "$remote_head" ]]; then
        # Ein früherer Lauf kann v1.05 bereits lokal committed haben und erst
        # danach beim Tag oder Push stehen geblieben sein. Nur dieser exakt
        # erkennbare einzelne Folgecommit darf aktualisiert werden.
        resume_committed=1
        printf '%s\n' "Sichere Fortsetzung des lokalen v1.05-Commits erkannt."
    else
        fail "Lokaler $branch und origin/$branch unterscheiden sich unerwartet. Nichts wurde geändert."
    fi
fi
if git -C "$project_dir" ls-remote --exit-code --tags origin "refs/tags/$tag" >/dev/null 2>&1; then
    fail "Tag $tag existiert bereits auf GitHub und wird nicht überschrieben."
fi
local_tag_commit=""
if git -C "$project_dir" show-ref --verify --quiet "refs/tags/$tag"; then
    local_tag_commit="$(git -C "$project_dir" rev-list -n 1 "$tag")"
    if [[ "$resume_committed" != 1 || "$local_tag_commit" != "$local_head" ]]; then
        fail "Lokaler Tag $tag gehört nicht zum sicher erkannten v1.05-Commit."
    fi
fi

# Nur die geprüfte öffentliche Positivliste kopieren. PCAP, PCAPNG und LZ4
# gelangen dadurch weder in das Original noch in Commit, Tag oder ZIP.
python3 "$source_dir/scripts/check_repository.py" --root "$source_dir"
python3 - "$source_dir" "$project_dir" <<'PY'
from pathlib import Path
import shutil
import sys

source = Path(sys.argv[1]).resolve()
target = Path(sys.argv[2]).resolve()
sys.path.insert(0, str(source / "scripts"))
from check_repository import public_files

for item in public_files(source):
    destination = target / item.relative_to(source)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(item, destination)
for obsolete in ("einspielen-v1.04.sh", "tests/parent_probe.cpp"):
    path = target / obsolete
    if path.is_file() or path.is_symlink():
        path.unlink()
PY

tracked_capture="$(git -C "$project_dir" ls-files |
    sed -nE '/\.(pcap|pcapng)(\..*)?$/Ip; /\.lz4$/Ip' | head -n 1)"
[[ -z "$tracked_capture" ]] ||
    fail "Mitschnitt ist versioniert und wird nicht committed: $tracked_capture"

python3 "$project_dir/scripts/check_repository.py" --root "$project_dir"
python3 -m unittest discover -s "$project_dir/tests" -p 'test_repository.py' -v

test_build="$project_dir/build/v1.05-update"
# Release ZIPs use deterministic timestamps. A retained build directory may
# therefore make old objects look newer than newly extracted sources. Always
# remove this dedicated test build before configuring it.
[[ "$test_build" == "$project_dir/build/v1.05-update" ]] ||
    fail "Unerwartetes Test-Buildverzeichnis: $test_build"
cmake -E remove_directory "$test_build"
cmake -S "$project_dir" -B "$test_build" \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build "$test_build" --parallel 2
ctest --test-dir "$test_build" --output-on-failure

if [[ "${AIRCTRL_SKIP_INSTALL:-0}" != "1" ]]; then
    pkill -x airctrl-desklet 2>/dev/null || true
    bash "$project_dir/install.sh"
fi

git -C "$project_dir" add -A
if git -C "$project_dir" diff --cached --quiet; then
    [[ "$resume_committed" == 1 ]] ||
        fail "Keine v1.05-Änderung zum Committen gefunden."
else
    git -C "$project_dir" diff --cached --check
    if [[ "$resume_committed" == 1 ]]; then
        git -C "$project_dir" commit --amend -m "$commit_message"
    else
        git -C "$project_dir" commit -m "$commit_message"
    fi
fi
if [[ -n "$local_tag_commit" ]]; then
    git -C "$project_dir" tag -d "$tag"
fi
git -C "$project_dir" tag -a "$tag" -m "AirCtrl-Desklet $tag"

if [[ "${AIRCTRL_SKIP_PUSH:-0}" != "1" ]]; then
    git -C "$project_dir" push --atomic origin "$branch" "refs/tags/$tag"
fi

printf '%s\n' "Fertig: Commit $(git -C "$project_dir" rev-parse --short HEAD), Tag $tag"
printf '%s\n' "Remote: $(git -C "$project_dir" remote get-url origin)"
if [[ "${AIRCTRL_SKIP_INSTALL:-0}" != "1" ]]; then
    printf '%s\n' "Server: systemctl --user status airctrl-server.service"
    printf '%s\n' "Desklet: ${HOME}/.local/bin/airctrl-desklet"
    printf '%s\n' "Client: ${HOME}/.local/bin/airctrl-client status"
fi
