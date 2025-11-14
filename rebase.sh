#!/bin/sh
set -eu

if git rebase --show-current-patch > /dev/null 2>&1; then
    echo "ERROR: There is already a rebase in progress. Manually resolve the conflicts or abort it with \"git rebase --abort\"" >&2
    exit 1
fi

# Ensure we are inside a Git working tree (not just a bare repo or .git dir)
if [ "$(git rev-parse --is-inside-work-tree 2>/dev/null)" != "true" ]; then
    echo "ERROR: This script must be run inside a Git working tree." >&2
    exit 1
fi

# Ensure we are on a real branch (not detached HEAD)
current_branch=$(git rev-parse --abbrev-ref HEAD)
if [ "${current_branch}" = "HEAD" ]; then
    echo "ERROR: This script must be run while on a local branch, not detached HEAD." >&2
    exit 1
fi

if ! tracking_ref=$(git rev-parse --abbrev-ref --symbolic-full-name '@{upstream}' 2>/dev/null); then
    echo "ERROR: Current branch '${current_branch}' has no configured upstream branch." >&2
    exit 1
fi

if [ "$tracking_ref" != "origin/${current_branch}" ]; then
    echo "ERROR: Current branch '${current_branch}' must track 'origin/${current_branch}'." >&2
    echo "Configured upstream is: $tracking_ref" >&2
    exit 1
fi

upstream_ref="upstream/trunk"

echo "Upstream ref: $upstream_ref"
echo "Local branch: ${current_branch}"
echo "Origin ref:   origin/${current_branch}"
echo

# Ensure clean working tree (no uncommitted changes)
if ! git diff-index --quiet HEAD --; then
    echo "ERROR: Working tree is not clean. Commit or stash your changes first." >&2
    exit 1
fi

# Make sure origin is up to date
git fetch origin

if ! git show-ref --verify --quiet "refs/remotes/origin/${current_branch}"; then
    echo "ERROR: Remote-tracking branch origin/${current_branch} does not exist." >&2
    exit 1
fi

# Abort if local branch has commits not on its origin tracking branch
if [ "$(git rev-list --count "origin/${current_branch}..${current_branch}")" -ne 0 ]; then
    echo "ERROR: ${current_branch} has unpushed commits relative to origin/${current_branch}; aborting reset." >&2
    exit 1
fi

# Reset local branch to its origin tracking branch and reset tags to match origin
git reset --hard "origin/${current_branch}"
git tag -l | xargs -r git tag -d > /dev/null 2>&1    # Delete all local tags
git fetch origin --tags > /dev/null 2>&1             # Re-fetch tags from origin

# Create temp files
old_commits=$(mktemp)
new_commits=$(mktemp)
tags_map=$(mktemp)
new_subjects=$(mktemp)

cleanup() {
    rm -f "$old_commits" "$new_commits" "$tags_map" "$new_subjects"
}
trap cleanup EXIT INT TERM

# Fetch latest upstream
echo "Fetching upstream..."
git fetch upstream
echo

# Collect commits ahead of upstream/trunk (old commits), oldest first
echo "Determining commits ahead of $upstream_ref..."
git rev-list --reverse "$upstream_ref..${current_branch}" >"$old_commits"

old_count=$(wc -l <"$old_commits" | tr -d ' ')
if [ "$old_count" -eq 0 ]; then
    echo "No commits ahead of $upstream_ref; nothing to do."
    exit 0
fi

echo "Commits ahead of $upstream_ref:"
while IFS= read -r sha; do
    subj=$(git log -1 --pretty='%s' "$sha")
    echo "  $sha  $subj"
done <"$old_commits"
echo

# Build a mapping: old_commit -> subject + tags (space-separated)
echo "Recording tags on ahead-of-upstream commits..."
: >"$tags_map"
tagged_count=0
while IFS= read -r sha; do
    [ -z "$sha" ] && continue
    subj=$(git log -1 --pretty='%s' "$sha")
    tags=$(git tag --points-at "$sha" || true)
    if [ -n "$tags" ]; then
        tags_space=$(printf '%s\n' "$tags" | tr '\n' ' ' | sed 's/ *$//')
        # Store as: sha<TAB>subject<TAB>tag1 tag2 ...
        printf '%s\t%s\t%s\n' "$sha" "$subj" "$tags_space" >>"$tags_map"
        echo "  $sha  $subj: $tags_space"
        tagged_count=$((tagged_count + 1))
    fi
done <"$old_commits"
echo

if [ "$tagged_count" -eq 0 ]; then
    echo "NOTE: No tags found on any commits ahead of $upstream_ref."
    echo "      There will be no tags to move after the rebase."
fi

# Ask how we want to rebase (normal vs interactive for squashing)
echo "Rebase options:"
echo "  1) Normal rebase (keep commits separate)"
echo "  2) Interactive rebase (allows squashing/reordering/editing)"
printf "Choose rebase mode [1/2, default 1]: "
read -r mode || mode="1"

case "$mode" in
    2)
        rebase_args="-i"
        ;;
    *)
        rebase_args=""
        ;;
esac
echo

echo "Rebasing ${current_branch} onto $upstream_ref..."
if [ -n "$rebase_args" ]; then
    echo "Using interactive rebase (you can squash commits in the editor)..."
fi

if ! git rebase ${rebase_args:+$rebase_args} "$upstream_ref"; then
    echo "ERROR: Rebase failed (likely due to conflicts)." >&2
    echo "       Resolve the conflicts and finish the rebase manually," >&2
    echo "       then run this script again when ${current_branch} is clean." >&2
    exit 1
fi
echo "Rebase complete."
echo

# Collect new commits ahead of upstream/trunk after rebase
echo "Determining commits ahead of $upstream_ref after rebase..."
git rev-list --reverse "$upstream_ref..${current_branch}" >"$new_commits"

new_count=$(wc -l <"$new_commits" | tr -d ' ')

echo "New commits ahead of $upstream_ref:"
: >"$new_subjects"
while IFS= read -r sha; do
    subj=$(git log -1 --pretty='%s' "$sha")
    # Store as: sha<TAB>subject
    printf '%s\t%s\n' "$sha" "$subj" >>"$new_subjects"
    echo "  $sha  $subj"
done <"$new_commits"
echo

if [ "$new_count" -ne "$old_count" ]; then
    echo "NOTE: Number of commits ahead of upstream changed after rebase."
    echo "      Old count: $old_count, new count: $new_count"
    echo "      This is expected if you squashed or dropped commits."
fi

# Reassign tags: try to match by commit subject
if [ "$tagged_count" -gt 0 ]; then
    echo "Reassigning tags from old commits to new commits (by subject match)..."

    # For each tagged old commit, look for a unique new commit with the same subject.
    while IFS= read -r line; do
        [ -z "$line" ] && continue
        old_sha=$(printf '%s\n' "$line" | cut -f1)
        old_subj=$(printf '%s\n' "$line" | cut -f2)
        tags=$(printf '%s\n' "$line" | cut -f3-)

        # Find new commits whose subject matches exactly
        matches=$(awk -F '\t' -v s="$old_subj" '$2 == s {print $1}' "$new_subjects" || true)
        match_count=$(printf '%s\n' "$matches" | sed '/^$/d' | wc -l | tr -d ' ')

        if [ "$match_count" -eq 0 ]; then
            echo "WARNING: No new commit found with matching subject for tagged commit:"
            echo "         $old_sha  $old_subj"
            echo "         Leaving tags on the old commit: $tags"
            echo
            continue
        fi

        if [ "$match_count" -gt 1 ]; then
            echo "WARNING: Multiple new commits found with matching subject for tagged commit:"
            echo "         $old_sha  $old_subj"
            echo "         Leaving tags on the old commit: $tags"
            echo
            continue
        fi

        new_sha=$(printf '%s\n' "$matches" | sed -n '1p')
        for t in $tags; do
            echo "  Moving tag $t: $old_sha -> $new_sha"
            git tag -f "$t" "$new_sha" >/dev/null 2>&1
        done
    done <"$tags_map"
    echo
else
    echo "No tags were moved (none were found on the ahead-of-upstream commits)."
fi

echo
echo "If everything looks correct, you probably want to push branch and tags:"
echo "  git push -f origin ${current_branch} --tags"
echo
