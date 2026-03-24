#!/bin/bash

# Git initialization script for JK BMS Master Controller
# Run this after cloning to set up your development environment

set -e  # Exit on error

echo "🚀 Initializing JK BMS Master Controller repository..."

# Check if git is installed
if ! command -v git &> /dev/null; then
    echo "❌ Git is not installed. Please install git first."
    exit 1
fi

# Initialize git if not already initialized
if [ ! -d ".git" ]; then
    echo "📦 Initializing Git repository..."
    git init
    git add .
    git commit -m "chore: initial commit with project structure"
    echo "✅ Git repository initialized"
else
    echo "ℹ️  Git repository already initialized"
fi

# Set up git hooks (optional)
echo "🔧 Setting up git hooks..."
mkdir -p .git/hooks

# Pre-commit hook (format check)
cat > .git/hooks/pre-commit << 'EOF'
#!/bin/bash
# Pre-commit hook: Check code formatting

echo "🔍 Running pre-commit checks..."

# Check if clang-format is installed
if command -v clang-format &> /dev/null; then
    echo "  Checking code formatting..."
    # Add actual formatting check here when clang-format config exists
else
    echo "  ⚠️  clang-format not found, skipping format check"
fi

# Check for TODOs in code (warning only)
if grep -rn "TODO\|FIXME" src/ include/ 2>/dev/null | grep -v "Binary file"; then
    echo "  ℹ️  Found TODOs in code (this is just a reminder)"
fi

echo "✅ Pre-commit checks complete"
exit 0
EOF

chmod +x .git/hooks/pre-commit
echo "✅ Git hooks installed"

# Configure git (if not already configured)
if [ -z "$(git config user.name)" ]; then
    echo ""
    echo "📝 Git user not configured. Please enter your details:"
    read -p "Your name: " git_name
    read -p "Your email: " git_email
    git config user.name "$git_name"
    git config user.email "$git_email"
    echo "✅ Git user configured"
fi

# Set up remote (if not exists)
if ! git remote | grep -q "origin"; then
    echo ""
    echo "🌐 No remote configured. Do you want to add one? (y/n)"
    read -p "> " add_remote
    if [ "$add_remote" = "y" ]; then
        read -p "Enter remote URL: " remote_url
        git remote add origin "$remote_url"
        echo "✅ Remote 'origin' added: $remote_url"
    fi
fi

# Check if PlatformIO is installed
echo ""
echo "🔧 Checking PlatformIO installation..."
if command -v pio &> /dev/null; then
    echo "✅ PlatformIO is installed"
    echo "   Run 'pio run' to build the project"
else
    echo "⚠️  PlatformIO not found"
    echo "   Install with: pip install platformio"
fi

echo ""
echo "✅ Setup complete!"
echo ""
echo "📚 Next steps:"
echo "  1. Review docs/FSD.md for project overview"
echo "  2. Check TODO.md for current tasks"
echo "  3. Configure WiFi credentials in src/main.cpp (lines 22-23)"
echo "  4. Build with: pio run"
echo "  5. Upload with: pio run --target upload"
echo ""
echo "💡 For development workflow, see CONTRIBUTING.md"
echo ""
