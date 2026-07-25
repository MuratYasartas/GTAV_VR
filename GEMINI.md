# Gemini Customization

This file allows you to customize how Gemini interacts with your project.

## General Instructions

-   **Focus:** Guide Gemini towards the most critical parts of the codebase.
-   **Conventions:** Outline specific coding conventions, style guides, or architectural patterns unique to this project.
-   **Priorities:** Define priorities for tasks (e.g., performance over readability in specific modules).

## Project-Specific Guidelines

### Key Directories

-   `OVRInject/`: This directory contains the core VR injection logic. Pay close attention to changes here.
-   `GTAVOVR/`: This is the main application.
-   `ThirdParty/`: External dependencies, generally avoid modifying these unless explicitly instructed.

### Coding Style

-   Follow existing C++ style, including naming conventions and formatting.
-   Prefer modern C++ features where appropriate.

### Testing

-   Describe how testing is done in this project.
-   Mention any specific testing frameworks or methodologies.

### Debugging

-   Mention any common debugging practices or tools.

## Example: Ignoring a file or directory

To tell Gemini to ignore a file or directory, you can create a `.geminiignore` file in the root of your project.

```
# .geminiignore
/tmp
/GTAVOVR/x64
```
