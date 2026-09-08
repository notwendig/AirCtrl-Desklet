#!/usr/bin/env bash
set -Eeuo pipefail

version="1.04"
tag="v${version}"
expected_remote="git@github.com:notwendig/AirCtrl-Desklet.git"
source_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
project_dir="${AIRCTRL_PROJECT_DIR:-${HOME}/Projects/Qt/AirCtrl-Desklet}"

fail() {
    printf 'FEHLER: %s\n' "$*" >&2
    exit 1
}

command -v git >/dev/null || fail "git fehlt."
command -v python3 >/dev/null || fail "python3 fehlt."
command -v cmake >/dev/null || fail "cmake fehlt."
command -v ctest >/dev/null || fail "ctest fehlt."

[[ -d "$project_dir/.git" ]] ||
    fail "Kein Git-Original gefunden: $project_dir"

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
    git -C "$project_dir" status --short >&2
    fail "Das Original enthält ungesicherte Änderungen. Nichts wurde kopiert."
fi

# Rohmitschnitte bleiben vollständig außerhalb von Kopie, Commit und ZIP.
# Auch versehentlich neben dem entpackten Quellbaum liegende Dateien werden
# ignoriert, weil ausschließlich die öffentliche Positivliste kopiert wird.
python3 "$source_dir/scripts/check_repository.py" --root "$source_dir"

python3 - "$source_dir" "$project_dir" <<'PY'
from pathlib import Path
import shutil
import sys

source = Path(sys.argv[1]).resolve()
target = Path(sys.argv[2]).resolve()
sys.path.insert(0, str(source / "scripts"))
from check_repository import public_files

if source != target:
    for item in public_files(source):
        destination = target / item.relative_to(source)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(item, destination)
PY

tracked_capture="$(git -C "$project_dir" ls-files | \
    sed -nE '/\.(pcap|pcapng)(\..*)?$/Ip; /\.lz4$/Ip' | head -n 1)"
if [[ -n "$tracked_capture" ]]; then
    fail "Mitschnitt ist versioniert und wird nicht committed: $tracked_capture"
fi

python3 "$project_dir/scripts/check_repository.py" --root "$project_dir"
python3 -m unittest discover -s "$project_dir/tests" -p 'test_repository.py' -v
test_build="$project_dir/build/v1.04-update"
cmake -S "$project_dir" -B "$test_build" \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build "$test_build" --parallel 2
ctest --test-dir "$test_build" --output-on-failure

if [[ "${AIRCTRL_SKIP_INSTALL:-0}" != "1" ]]; then
    pkill -x airctrl-desklet 2>/dev/null || true
    pkill -x airctrl-backend 2>/dev/null || true
    bash "$project_dir/install.sh"
fi

git -C "$project_dir" add -A

if git -C "$project_dir" diff --cached --quiet; then
    printf '%s\n' "Keine neue Änderung; vorhandener Stand wird getaggt/gepusht."
else
    if git -C "$project_dir" show-ref --verify --quiet "refs/tags/$tag"; then
        git -C "$project_dir" restore --staged .
        fail "Tag $tag existiert bereits; er wird nicht verschoben oder überschrieben."
    fi
    git -C "$project_dir" diff --cached --check
    git -C "$project_dir" commit -m "v1.04: document real-device I/O validation"
fi

if git -C "$project_dir" show-ref --verify --quiet "refs/tags/$tag"; then
    tag_commit="$(git -C "$project_dir" rev-list -n 1 "$tag")"
    head_commit="$(git -C "$project_dir" rev-parse HEAD)"
    [[ "$tag_commit" == "$head_commit" ]] ||
        fail "Tag $tag zeigt nicht auf HEAD; kein Tag wird verändert."
else
    git -C "$project_dir" tag -a "$tag" -m "AirCtrl-Desklet $tag"
fi

if git -C "$project_dir" remote get-url origin >/dev/null 2>&1; then
    git -C "$project_dir" remote set-url origin "$expected_remote"
else
    git -C "$project_dir" remote add origin "$expected_remote"
fi

if [[ "${AIRCTRL_SKIP_PUSH:-0}" != "1" ]]; then
    git -C "$project_dir" push --atomic origin "$branch" "refs/tags/$tag"
fi

printf '%s\n' "Fertig: Commit $(git -C "$project_dir" rev-parse --short HEAD), Tag $tag"
printf '%s\n' "Remote: $(git -C "$project_dir" remote get-url origin)"
if [[ "${AIRCTRL_SKIP_INSTALL:-0}" != "1" ]]; then
    printf '%s\n' "Installiert. Start: ${HOME}/.local/bin/airctrl-desklet"
fi
