// C# sample
using System;
using System.Collections.Generic;

namespace Sample
{
    public class Calculator
    {
        public int Add(int a, int b) => a + b;

        public static void Main(string[] args)
        {
            var calc = new Calculator();
            Console.WriteLine($"Result: {calc.Add(2, 3)}");
            var items = new List<string> { "one", "two" };
        }
    }
}
