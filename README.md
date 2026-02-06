# Minimal Windows Text Editor

A lightweight, self-hosting text editor written in C.

> **Fun Fact:** Some of the code for this editor was written using the editor itself! 🤯

![Screenshot](./screenshot.png)

## Features
*   **Syntax Highlighting** (C/C++)
*   **Search** (Ctrl+F)
*   **Saving** (Ctrl+S)
*   **Zero Dependencies** (Uses Windows API)

## Build
Uses [Tiny C Compiler (TCC)](https://github.com/cnlohr/tinycc-win64-installer/releases).

```powershell
& "C:\Program Files (x86)\tcc-0.9.27\tcc.exe" editor.c -o editor.exe
```

## Usage
```powershell
.\editor.exe [filename]
```

## Controls
| Key | Action |
| :--- | :--- |
| **Ctrl+Q** | Quit |
| **Ctrl+S** | Save |
| **Ctrl+F** | Find |
