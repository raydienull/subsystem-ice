# Contributing to OnlineSubsystemICE

Thank you for your interest in contributing to OnlineSubsystemICE! This document provides guidelines and instructions for contributing.

## Table of Contents

- [Code of Conduct](#code-of-conduct)
- [Getting Started](#getting-started)
- [Development Setup](#development-setup)
- [How to Contribute](#how-to-contribute)
- [Coding Standards](#coding-standards)
- [Commit Guidelines](#commit-guidelines)
- [Pull Request Process](#pull-request-process)
- [Reporting Bugs](#reporting-bugs)
- [Suggesting Features](#suggesting-features)

## Code of Conduct

### Our Pledge

We are committed to providing a welcoming and inclusive environment for all contributors. We expect all participants to:

- Use welcoming and inclusive language
- Be respectful of differing viewpoints and experiences
- Gracefully accept constructive criticism
- Focus on what is best for the community
- Show empathy towards other community members

## Getting Started

1. **Fork the Repository**
   ```bash
   # Fork on GitHub, then clone your fork
   git clone https://github.com/YOUR_USERNAME/subsystem-ice.git
   cd subsystem-ice
   ```

2. **Add Upstream Remote**
   ```bash
   git remote add upstream https://github.com/raydienull/subsystem-ice.git
   ```

3. **Create a Branch**
   ```bash
   git checkout -b feature/your-feature-name
   # or
   git checkout -b fix/your-bug-fix
   ```

## Development Setup

### Prerequisites

- Unreal Engine 5.6 or later
- Visual Studio 2022 (Windows) or Xcode (Mac) or Clang (Linux)
- Git

### Building the Plugin

1. Copy the plugin to your test project's `Plugins` folder
2. Regenerate project files
3. Build the project in your IDE

### Running Tests

Currently, testing is done by integrating the plugin into a test project:

1. Create a new Unreal Engine project
2. Add OnlineSubsystemICE to the Plugins folder
3. Configure DefaultEngine.ini
4. Test session creation, joining, and P2P connectivity

## How to Contribute

### Types of Contributions

We welcome various types of contributions:

- **Bug Fixes**: Fix issues reported in GitHub Issues
- **Features**: Implement new functionality
- **Documentation**: Improve or add documentation
- **Tests**: Add or improve test coverage
- **Examples**: Create example projects or code snippets
- **Performance**: Optimize existing code
- **Refactoring**: Improve code quality

### Areas Needing Contribution

High-priority areas:

1. **Signaling Server Implementation**
   - WebSocket-based signaling
   - Candidate exchange mechanism
   - Session discovery protocol

2. **Full TURN Support**
   - Complete RFC 5766 implementation
   - Allocation management
   - Channel binding

3. **Security**
   - DTLS encryption for P2P channels
   - Authentication improvements
   - Token-based authorization

4. **Testing**
   - Unit tests
   - Integration tests
   - NAT traversal test suite

5. **Platform Support**
   - Console platforms
   - Mobile platforms
   - Additional desktop platforms

## Coding Standards

### Unreal Engine Coding Standard

Follow the [Unreal Engine Coding Standard](https://docs.unrealengine.com/en-US/ProductionPipelines/DevelopmentSetup/CodingStandard/index.html):

- Use PascalCase for class names: `FOnlineSubsystemICE`
- Use PascalCase for function names: `CreateSession`
- Use camelCase for local variables: `sessionName`
- Use PascalCase with prefix for member variables: `SessionInterface`
- Use `b` prefix for booleans: `bIsConnected`

### Code Style

```cpp
// Good
class FMyClass
{
public:
    void DoSomething(int32 Parameter);
    
private:
    int32 MemberVariable;
    bool bIsEnabled;
};

void FMyClass::DoSomething(int32 Parameter)
{
    if (bIsEnabled)
    {
        MemberVariable = Parameter;
    }
}
```

### Header Organization

```cpp
// Copyright notice

#pragma once

// Engine includes
#include "CoreMinimal.h"
#include "OnlineSubsystem.h"

// Plugin includes
#include "OnlineSubsystemICEPackage.h"

// Generated include (for UCLASS/USTRUCT)
#include "MyClass.generated.h"

// Class declaration
class FMyClass
{
    // Public interface first
    // Protected members
    // Private members last
};
```

### Logging

Use appropriate log categories:

```cpp
UE_LOG(LogOnlineICE, Log, TEXT("Normal operation"));
UE_LOG(LogOnlineICE, Warning, TEXT("Potential issue"));
UE_LOG(LogOnlineICE, Error, TEXT("Error occurred"));
UE_LOG(LogOnlineICE, Verbose, TEXT("Detailed information"));
UE_LOG(LogOnlineICE, VeryVerbose, TEXT("Very detailed information"));
```

### Comments

- Write clear, concise comments
- Document public APIs thoroughly
- Explain complex algorithms
- Use Doxygen-style comments for functions

```cpp
/**
 * Creates a new online session
 * @param HostingPlayerNum - Local player hosting the session
 * @param SessionName - Name of the session to create
 * @param NewSessionSettings - Settings for the new session
 * @return true if session creation started successfully
 */
bool CreateSession(int32 HostingPlayerNum, FName SessionName, const FOnlineSessionSettings& NewSessionSettings);
```

## Commit Guidelines

### Commit Message Format

```
type(scope): subject

body (optional)

footer (optional)
```

### Types

- `feat`: New feature
- `fix`: Bug fix
- `docs`: Documentation changes
- `style`: Code style changes (formatting, etc.)
- `refactor`: Code refactoring
- `perf`: Performance improvements
- `test`: Adding or updating tests
- `chore`: Maintenance tasks

### Examples

```
feat(ice): add IPv6 support for candidate gathering

- Implement IPv6 address handling
- Add configuration option for IPv6
- Update STUN request to support IPv6

Closes #123
```

```
fix(session): prevent duplicate session creation

Check if session already exists before creating a new one.

Fixes #456
```

### Best Practices

- Use present tense ("add feature" not "added feature")
- Use imperative mood ("move cursor to..." not "moves cursor to...")
- Limit first line to 72 characters
- Reference issues and pull requests in footer

## Pull Request Process

### Before Submitting

1. **Sync with Upstream**
   ```bash
   git fetch upstream
   git rebase upstream/main
   ```

2. **Test Your Changes**
   - Build successfully on target platforms
   - Test the specific functionality you changed
   - Verify no regressions

3. **Update Documentation**
   - Update README if needed
   - Add/update code comments
   - Update CHANGELOG.md

4. **Check Code Style**
   - Follow Unreal Engine coding standards
   - Run any available linters

### Submitting PR

1. **Push to Your Fork**
   ```bash
   git push origin feature/your-feature-name
   ```

2. **Create Pull Request**
   - Go to GitHub and create a PR from your fork
   - Use a clear, descriptive title
   - Fill out the PR template
   - Link related issues

3. **PR Description Template**
   ```markdown
   ## Description
   Brief description of the changes
   
   ## Type of Change
   - [ ] Bug fix
   - [ ] New feature
   - [ ] Breaking change
   - [ ] Documentation update
   
   ## Testing
   Describe how you tested your changes
   
   ## Checklist
   - [ ] Code follows project style guidelines
   - [ ] Self-review completed
   - [ ] Comments added for complex code
   - [ ] Documentation updated
   - [ ] No new warnings generated
   - [ ] Changes tested on target platforms
   
   ## Related Issues
   Closes #123
   ```

### Review Process

1. Maintainers will review your PR
2. Address any requested changes
3. Once approved, your PR will be merged

### After Merge

1. **Delete Your Branch**
   ```bash
   git branch -d feature/your-feature-name
   git push origin --delete feature/your-feature-name
   ```

2. **Sync Your Fork**
   ```bash
   git checkout main
   git fetch upstream
   git merge upstream/main
   git push origin main
   ```

## Reporting Bugs

### Before Reporting

1. Check existing issues to avoid duplicates
2. Test with the latest version
3. Verify it's not a configuration issue

### Bug Report Template

```markdown
**Describe the Bug**
A clear description of the bug

**To Reproduce**
Steps to reproduce:
1. Go to '...'
2. Click on '...'
3. See error

**Expected Behavior**
What you expected to happen

**Actual Behavior**
What actually happened

**Environment**
- OS: [e.g., Windows 11]
- Unreal Engine Version: [e.g., 5.6]
- Plugin Version: [e.g., 2.0.0]

**Logs**
```
Paste relevant log entries
```

**Additional Context**
Any other relevant information
```

### Severity Levels

- **Critical**: Crash or data loss
- **High**: Major feature broken
- **Medium**: Feature partially broken
- **Low**: Minor issue or cosmetic

## Suggesting Features

### Feature Request Template

```markdown
**Problem Statement**
Describe the problem or need

**Proposed Solution**
Describe your proposed solution

**Alternatives Considered**
Other approaches you've considered

**Additional Context**
Mockups, examples, or references

**Implementation Complexity**
Your assessment of implementation difficulty
```

### Feature Discussion

1. Open an issue with the feature request
2. Discuss with maintainers and community
3. Once approved, implementation can begin
4. Submit PR referencing the feature request issue

## Development Workflow

### Typical Workflow

1. Pick an issue or create one
2. Discuss approach in the issue
3. Fork and create branch
4. Implement changes
5. Test thoroughly
6. Submit PR
7. Address review feedback
8. Merge!

### Communication

- Use GitHub Issues for bug reports and feature requests
- Use GitHub Discussions for general questions
- Tag maintainers if you need attention on an issue/PR

## Testing Guidelines

### Manual Testing

Always test:
- Session creation and destruction
- Session joining
- Player authentication
- Candidate gathering
- Connection establishment

### Test Platforms

When possible, test on:
- Windows 10/11
- Linux (Ubuntu 20.04+)
- macOS (10.15+)

### Network Scenarios

Test with:
- Same LAN
- Different networks
- Various NAT types
- Firewall enabled/disabled

## Documentation

### Documentation Types

- **Code Comments**: Inline explanations
- **API Documentation**: Public interface docs
- **User Guides**: How-to guides
- **Architecture Docs**: System design documents

### Updating Documentation

When changing functionality:
1. Update relevant code comments
2. Update README if user-facing
3. Update integration guides
4. Update API documentation

## Community

### Getting Help

- Read existing documentation first
- Search closed issues
- Ask in GitHub Discussions
- Tag relevant maintainers

### Helping Others

- Answer questions in Discussions
- Review pull requests
- Improve documentation
- Share your experiences

## License

By contributing, you agree that your contributions will be licensed under the MIT License.

## Recognition

Contributors will be recognized in:
- CHANGELOG.md
- Project README
- Release notes

Thank you for contributing to OnlineSubsystemICE! 🎉
