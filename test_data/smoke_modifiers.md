# Smoke test — theme modifiers

Default theme should render `// comments` italic and preprocessor directives bold.

```cpp
#include <iostream>
#include <vector>

// This is a single-line comment.
// Italic glyphs should be visibly slanted compared to the code.

#define BUFFER_SIZE 256

int main() {
    /* This block comment should also be italic. */
    std::vector<int> v = {1, 2, 3};
    return 0;
}
```

```python
# Python comments should also be italic.
def greet(name):
    """Docstrings are strings, NOT comments — should NOT be italic."""
    print(f"Hello, {name}")
```

Plain prose paragraph below the code blocks for visual contrast — should be unaffected.
