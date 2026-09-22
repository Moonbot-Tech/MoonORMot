#!/bin/bash
# bump-version.sh — raise moonormot.version.inc when a push brought code
# without raising the number.
# Arguments: BEFORE_SHA  AFTER_SHA
# Exits 0 always; prints what it did.
#
# The rule is "+1 with every commit that changes code".  A push may carry
# several commits, so the commits are examined one by one: a commit that
# changes a code file and leaves the version file alone breaks the rule, and
# one bump at the tip repairs it — whatever the other commits of the same
# push did with the number.  Looking at the summary diff of the push instead
# would miss the case "commit 1 raises the number, commit 2 changes code
# without raising it": the tip would then carry code that a tree with the
# same number does not, and the consumers' version floors could not tell
# the two apart.

set -euo pipefail

BEFORE="$1"
AFTER="$2"
VERSION_FILE="moonormot.version.inc"

# Code extensions that count as "code changed"
CODE_PATTERN='\.(pas|inc|dfm|res|a|o|lib|dll)$'

# If BEFORE is the zero SHA or unreachable, fall back to HEAD~1
if [ "$BEFORE" = "0000000000000000000000000000000000000000" ] || ! git cat-file -e "$BEFORE" 2>/dev/null; then
  BEFORE="$AFTER~1"
  if ! git cat-file -e "$BEFORE" 2>/dev/null; then
    echo "Initial commit, no parent — skipping."
    exit 0
  fi
fi

# The commits of the push, oldest first (merges included: their diff
# against the first parent is what the branch received).
UNBUMPED=""
for commit in $(git rev-list --reverse --first-parent "$BEFORE".."$AFTER"); do
  code_changed=false
  version_changed=false
  while IFS= read -r f; do
    [ -z "$f" ] && continue
    if [ "$f" = "$VERSION_FILE" ]; then
      version_changed=true
    elif echo "$f" | grep -qE "$CODE_PATTERN"; then
      code_changed=true
    fi
  done < <(git diff-tree --no-commit-id --name-only -r -m --first-parent "$commit")
  if [ "$code_changed" = true ] && [ "$version_changed" = false ]; then
    UNBUMPED="$UNBUMPED ${commit:0:10}"
  fi
done

if [ -n "$UNBUMPED" ]; then
  # Read current version, increment, write back
  CURRENT=$(cat "$VERSION_FILE" | tr -d '[:space:]')
  NEW=$((CURRENT + 1))
  printf '%d' "$NEW" > "$VERSION_FILE"

  git config user.name "github-actions[bot]"
  git config user.email "41898282+github-actions[bot]@users.noreply.github.com"
  git add "$VERSION_FILE"
  git commit -m "MoonORMot: версия $NEW (автоподъём)" \
    -m "Коммиты с правкой кода без подъёма номера:$UNBUMPED"
  git push

  echo "Version bumped: $CURRENT -> $NEW (code commits without a bump:$UNBUMPED)"
else
  echo "Every code commit of the push raised the version, or no code changed — nothing to do."
fi
