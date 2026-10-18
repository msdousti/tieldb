# Code Formatting and Linting

This project enforces style with `clang-format` and `clang-tidy`. All code must pass before merging.

---

## Quick Start

```bash
# Format all C files (run before commit)
clang-format -i src/**/*.{c,h} tests/**/*.{c,h}

# Check formatting without changing files
clang-format --dry-run --Werror src/**/*.{c,h}

# Run linter
clang-tidy src/*.c -- -Isrc

# Run linter with compile_commands.json (after meson setup)
clang-tidy -p build-asan src/*.c
```

---

## Setup

### Install Tools (Linux)
```bash
# Debian/Ubuntu
sudo apt install clang clang-format clang-tidy

# Arch Linux
sudo pacman -S clang

# Fedora
sudo dnf install clang clang-tools-extra
```

### Install Tools (macOS)
```bash
# Using Homebrew
brew install llvm

# Add to PATH (add to ~/.zshrc or ~/.bashrc)
export PATH="/opt/homebrew/opt/llvm/bin:$PATH"
```

### Editor Integration

**VS Code:**
Install "C/C++" extension by Microsoft. It uses `.clang-format` automatically.

**Vim/Neovim:**
```vim
" Using vim-plug
Plug 'rhysd/vim-clang-format'

" Format on save
autocmd FileType c,cpp ClangFormatAutoEnable
```

**Emacs:**
```elisp
(require 'clang-format)
(add-hook 'c-mode-hook
          (lambda () (add-hook 'before-save-hook 'clang-format-buffer nil 'local)))
```

---

## Configuration Files

- **`.clang-format`** - Code formatting rules
- **`.clang-tidy`** - Static analysis checks
- **`.editorconfig`** - Editor settings (indent, line endings)

These files are committed to the repo. Do not modify without team discussion.

---

## Pre-commit Hook (Recommended)

Create `.git/hooks/pre-commit`:

```bash
#!/bin/bash
# Format check before commit

set -e

# Get list of staged C files
FILES=$(git diff --cached --name-only --diff-filter=ACM | grep -E '\.(c|h)$' || true)

if [ -n "$FILES" ]; then
    echo "Checking code format..."
    
    # Check format
    clang-format --dry-run --Werror $FILES
    
    if [ $? -ne 0 ]; then
        echo "❌ Format check failed. Run: clang-format -i $FILES"
        exit 1
    fi
    
    echo "✅ Format check passed"
fi

exit 0
```

Make it executable:
```bash
chmod +x .git/hooks/pre-commit
```

---

## CI Integration

In CI (GitHub Actions, GitLab CI, etc.), run:

```bash
# Check formatting
clang-format --dry-run --Werror src/**/*.{c,h} tests/**/*.{c,h}

# Run static analysis
clang-tidy -p build-asan src/*.c
```

---

## Common Issues

### "clang-format not found"
Install LLVM tools (see Setup above).

### "clang-tidy reports warnings in system headers"
Use `HeaderFilterRegex` in `.clang-tidy` (already configured).

### "clang-format changed too much"
Run it frequently. Big reformats are painful; small incremental runs are not.

### "I disagree with a rule"
Discuss with the team first. Style consistency > personal preference.

---

## Style Summary

See `style-guide.md` for full details. Quick reference:

- **Naming:** `snake_case` for functions/variables, `UPPER_CASE` for macros/constants
- **Indentation:** 4 spaces, no tabs
- **Line length:** 100 characters max
- **Braces:** Functions on new line, control structures on same line
- **Pointers:** Asterisk with name (`int *ptr`)
- **Always use braces** for single-statement blocks

---

## Example

**Before formatting:**
```c
int foo(int* x,int y){
if(x==NULL)return -1;
for(int i=0;i<y;i++){printf("%d\n",i);}
return 0;}
```

**After `clang-format -i`:**
```c
int foo(int *x, int y)
{
    if (x == NULL) {
        return -1;
    }
    for (int i = 0; i < y; i++) {
        printf("%d\n", i);
    }
    return 0;
}
```
