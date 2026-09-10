#!/usr/bin/env bash
set -Eeuo pipefail

version="1.06"
tag="v${version}"
architecture_tag="server_clients"
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
create_version_tag=1
if git -C "$project_dir" ls-remote --exit-code --tags origin "refs/tags/$tag" >/dev/null 2>&1; then
    create_version_tag=0
    printf '%s\n' "Hinweis: Der veröffentlichte Tag $tag bleibt unverändert."
elif git -C "$project_dir" show-ref --verify --quiet "refs/tags/$tag"; then
    fail "Lokaler Tag $tag existiert, aber nicht auf GitHub."
fi
if git -C "$project_dir" ls-remote --exit-code --tags origin "refs/tags/$architecture_tag" >/dev/null 2>&1; then
    fail "Tag $architecture_tag existiert bereits auf GitHub."
fi
git -C "$project_dir" show-ref --verify --quiet "refs/tags/$architecture_tag" &&
    fail "Lokaler Tag $architecture_tag existiert bereits."

# Publish from a detached worktree based directly on origin/master. This keeps
# the push fast-forward even when the maintainer's clean local master contains
# an older unpublished commit. That old tip is retained on a backup branch.
publish_dir="$(mktemp -d "${TMPDIR:-/tmp}/airctrl-server-clients.XXXXXX")"
rmdir "$publish_dir"
worktree_added=0
created_tags=()
publication_complete=0
cleanup() {
    if [[ "$worktree_added" == 1 ]]; then
        git -C "$project_dir" worktree remove --force "$publish_dir" >/dev/null 2>&1 || true
    fi
    if [[ "${#created_tags[@]}" -gt 0 && "$publication_complete" == 0 ]]; then
        git -C "$project_dir" tag -d "${created_tags[@]}" >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT
git -C "$project_dir" worktree add --detach "$publish_dir" origin/master
worktree_added=1

# Copy only the reviewed public allow-list into the publication worktree.
# Captures and build directories therefore cannot enter the commit or ZIP.
python3 - "$source_dir" "$publish_dir" <<'PY'
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

python3 "$publish_dir/scripts/check_repository.py" --root "$publish_dir"
tracked_capture="$(git -C "$publish_dir" ls-files | sed -nE '/\.(pcap|pcapng)(\..*)?$/Ip; /\.lz4$/Ip' | head -n 1)"
[[ -z "$tracked_capture" ]] || fail "Mitschnitt ist versioniert: $tracked_capture"

git -C "$publish_dir" add -A
git -C "$publish_dir" diff --cached --check
git -C "$publish_dir" diff --cached --quiet && fail "Keine v1.06-Änderung gefunden."
git -C "$publish_dir" commit -m "$commit_message"
published_commit="$(git -C "$publish_dir" rev-parse HEAD)"
if [[ "$create_version_tag" == 1 ]]; then
    git -C "$publish_dir" tag -a "$tag" -m "AirCtrl-Desklet $tag"
    created_tags+=("$tag")
fi
git -C "$publish_dir" tag -a "$architecture_tag" -m "AirCtrl server/client architecture"
created_tags+=("$architecture_tag")

if [[ "${AIRCTRL_SKIP_PUSH:-0}" != "1" ]]; then
    push_refs=("HEAD:master" "refs/tags/$architecture_tag")
    if [[ "$create_version_tag" == 1 ]]; then
        push_refs+=("refs/tags/$tag")
    fi
    git -C "$publish_dir" push --atomic origin "${push_refs[@]}"
fi
publication_complete=1

# Keep the old local tip recoverable, then make the checked-out master exactly
# the commit that was just published. No rebase and no force-push are used.
old_commit="$(git -C "$project_dir" rev-parse HEAD)"
backup_branch="backup-before-server-clients-${old_commit:0:8}"
git -C "$project_dir" show-ref --verify --quiet "refs/heads/$backup_branch" &&
    fail "Sicherungsbranch existiert bereits: $backup_branch"
git -C "$project_dir" branch "$backup_branch" "$old_commit"
git -C "$project_dir" worktree remove --force "$publish_dir"
worktree_added=0
git -C "$project_dir" switch --detach "$published_commit"
git -C "$project_dir" branch -f master "$published_commit"
git -C "$project_dir" switch master

if [[ "${AIRCTRL_SKIP_INSTALL:-0}" != "1" ]]; then
    pkill -x airctrl-desklet 2>/dev/null || true
    bash "$project_dir/install.sh"
fi

if [[ "$create_version_tag" == 1 ]]; then
    printf '%s\n' "Fertig: Commit $(git -C "$project_dir" rev-parse --short HEAD), Tags $tag und $architecture_tag"
else
    printf '%s\n' "Fertig: Commit $(git -C "$project_dir" rev-parse --short HEAD), neuer Tag $architecture_tag; $tag blieb unverändert"
fi
printf '%s\n' "Vorheriger lokaler Stand: $backup_branch"
printf '%s\n' 'Serverkonfiguration: /etc/airctrld.cfg'
printf '%s\n' "Serverstatus: systemctl --user status airctrl-server.service"
printf '%s\n' "Desklet: ${HOME}/.local/bin/airctrl-desklet"
