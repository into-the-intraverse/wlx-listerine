// JavaScript sample
const greet = (name) => `Hello, ${name}!`;

function factorial(n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

class Logger {
    constructor(prefix) {
        this.prefix = prefix;
    }
    log(message) {
        console.log(`[${this.prefix}] ${message}`);
    }
}

export default Logger;
