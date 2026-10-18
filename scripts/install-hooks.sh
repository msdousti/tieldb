#!/bin/bash
# Install git hooks for TielDB

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GIT_HOOKS_DIR="$(git rev-parse --git-dir)/hooks"

echo "Installing TielDB git hooks..."

# Install pre-commit hook
if [ -f "$GIT_HOOKS_DIR/pre-commit" ]; then
    echo "⚠️  Pre-commit hook already exists"
    read -p "   Overwrite? (y/N) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "Skipping pre-commit hook"
        exit 0
    fi
fi

cp "$SCRIPT_DIR/pre-commit" "$GIT_HOOKS_DIR/pre-commit"
chmod +x "$GIT_HOOKS_DIR/pre-commit"

echo "✅ Pre-commit hook installed"
echo ""
echo "The hook will:"
echo "  - Check code formatting with clang-format"
echo "  - Run static analysis with clang-tidy (if build-asan exists)"
echo ""
echo "To bypass the hook (not recommended):"
echo "  git commit --no-verify"
echo ""
echo "To uninstall:"
echo "  rm $GIT_HOOKS_DIR/pre-commit"
