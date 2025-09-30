---
applyTo: "**"
---
# Project general coding standards

## Naming Conventions
- Use camelCase for variables, functions, and methods for C++ code
- Use snake_case for variables, functions, and methods for C code
- In C always append openning { on a new line
- Use ALL_CAPS for constants in #define in C
- Use PascalCase for class names in C++
- All comments should be in English and should be clear and concise
- In if, for, while, and switch statements, single-line statements should not use braces, but multi-line statements should always use braces
- In C++ constructor implementations, put : at the end of the function declaration on the first line. Add all member initialization on a new line with the , at the end of each line.

Example:
```
MyClass::MyClass():
    MyBaseClass(a, b),
    member_1(3),
    member_2(nullptr)
{
}
```

## Error Handling
- Always log errors with contextual information
