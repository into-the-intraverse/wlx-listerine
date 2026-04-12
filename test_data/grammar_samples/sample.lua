-- Lua sample
local function fibonacci(n)
    if n <= 1 then return n end
    return fibonacci(n - 1) + fibonacci(n - 2)
end

local animals = {"cat", "dog", "bird"}

for i, name in ipairs(animals) do
    print(string.format("%d: %s", i, name))
end

local result = fibonacci(10)
print("Fib(10) = " .. tostring(result))
