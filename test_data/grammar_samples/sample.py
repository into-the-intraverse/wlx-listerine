# Python sample
import os
from typing import List

def fibonacci(n: int) -> List[int]:
    """Return first n Fibonacci numbers."""
    result = [0, 1]
    for i in range(2, n):
        result.append(result[-1] + result[-2])
    return result

class Counter:
    def __init__(self, start: int = 0):
        self.value = start

if __name__ == "__main__":
    print(fibonacci(10))
