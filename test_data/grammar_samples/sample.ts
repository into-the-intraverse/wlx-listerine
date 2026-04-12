// TypeScript sample
interface User {
    name: string;
    age: number;
}

function greet(user: User): string {
    return `Hello, ${user.name}!`;
}

const numbers: number[] = [1, 2, 3];
const doubled = numbers.map((n) => n * 2);

enum Color { Red, Green, Blue }

export { greet, Color };
