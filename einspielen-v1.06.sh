#!/usr/bin/env bash
set -Eeuo pipefail

version="1.06"
tag="v${version}"
commit_message="v1.06: TCP server and system device configuration"
expected_remote="${AIRCTRL_EXPECTED_REMOTE:-git@github.com:notwendig/AirCtrl-Desklet.git}"
source_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
project_dir="${AIRCTRL_PROJECT_DIR:-${HOME}/Projects/Qt/AirCtrl-Desklet}"

fail() {
    printf 'FEHLER: %s\n' "$*" >&2
    exit 1
}

for command_name in git python3 cmake ctest; do
    command -v "$command_name" >/dev/null || fail "$command_name fehlt."
done

# Erst das entpackte Paket vollständig prüfen. Das Git-Original bleibt bei
# einem Build- oder Testfehler unverändert.
python3 "$source_dir/scripts/check_repository.py" --root "$source_dir"
python3 -m unittest discover -s "$source_dir/tests" -p 'test_repository.py' -v
test_build="$source_dir/build/v1.06-package-test"
[[ "$test_build" == "$source_dir/build/v1.06-package-test" ]] || fail "Unerwartetes Test-Buildverzeichnis."
cmake -E remove_directory "$test_build"
cmake -S "$source_dir" -B "$test_build" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build "$test_build" --parallel 2
ctest --test-dir "$test_build" --output-on-failure

[[ -d "$project_dir/.git" ]] || fail "Kein Git-Original gefunden: $project_dir"
project_dir="$(cd -- "$project_dir" && pwd -P)"
git_root="$(git -C "$project_dir" rev-parse --show-toplevel)"
[[ "$git_root" == "$project_dir" ]] || fail "Das Ziel ist nicht die Repository-Wurzel."
branch="$(git -C "$project_dir" symbolic-ref --quiet --short HEAD)" || fail "Detached HEAD."
[[ "$branch" == "master" ]] || fail "Erwartet wird Branch master, gefunden: $branch"
[[ -z "$(git -C "$project_dir" status --porcelain --untracked-files=all)" ]] ||
    fail "Das Git-Original enthält Änderungen. Nichts wurde überschrieben."
[[ -n "$(git -C "$project_dir" config --get user.name || true)" ]] || fail "Git-Benutzername fehlt."
[[ -n "$(git -C "$project_dir" config --get user.email || true)" ]] || fail "Git-E-Mail-Adresse fehlt."

if git -C "$project_dir" remote get-url origin >/dev/null 2>&1; then
    git -C "$project_dir" remote set-url origin "$expected_remote"
else
    git -C "$project_dir" remote add origin "$expected_remote"
fi
git -C "$project_dir" fetch origin refs/heads/master:refs/remotes/origin/master
read -r ahead behind < <(git -C "$project_dir" rev-list --left-right --count master...origin/master)
[[ "$ahead" == 0 && "$behind" == 0 ]] ||
    fail "Lokaler master und origin/master sind nicht identisch. Nichts wurde geändert."
if git -C "$project_dir" ls-remote --exit-code --tags origin "refs/tags/$tag" >/dev/null 2>&1; then
    fail "Tag $tag existiert bereits auf GitHub."
fi
git -C "$project_dir" show-ref --verify --quiet "refs/tags/$tag" && fail "Lokaler Tag $tag existiert bereits."

# Ausschließlich die geprüfte öffentliche Positivliste kopieren. Mitschnitte
# und Buildverzeichnisse können dadurch weder in Commit noch ZIP gelangen.
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
for obsolete in ("tests/parent_probe.cpp",):
    path = target / obsolete
    if path.is_file() or path.is_symlink():
        path.unlink()
PY

python3 "$project_dir/scripts/check_repository.py" --root "$project_dir"
tracked_capture="$(git -C "$project_dir" ls-files | sed -nE '/\.(pcap|pcapng)(\..*)?$/Ip; /\.lz4$/Ip' | head -n 1)"
[[ -z "$tracked_capture" ]] || fail "Mitschnitt ist versioniert: $tracked_capture"

if [[ "${AIRCTRL_SKIP_INSTALL:-0}" != "1" ]]; then
    pkill -x airctrl-desklet 2>/dev/null || true
    bash "$project_dir/install.sh"
fi

git -C "$project_dir" add -A
git -C "$project_dir" diff --cached --check
git -C "$project_dir" diff --cached --quiet && fail "Keine v1.06-Änderung gefunden."
git -C "$project_dir" commit -m "$commit_message"
git -C "$project_dir" tag -a "$tag" -m "AirCtrl-Desklet $tag"

if [[ "${AIRCTRL_SKIP_PUSH:-0}" != "1" ]]; then
    git -C "$project_dir" push --atomic origin master "refs/tags/$tag"
fi

printf '%s\n' "Fertig: Commit $(git -C "$project_dir" rev-parse --short HEAD), Tag $tag"
printf '%s\n' 'Serverkonfiguration: /etc/airctrld.cfg'
printf '%s\n' "Serverstatus: systemctl --user status airctrl-server.service"
printf '%s\n' "Desklet: ${HOME}/.local/bin/airctrl-desklet"
