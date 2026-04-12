// Go sample
package main

import "fmt"

type Animal struct {
    Name string
    Age  int
}

func (a Animal) Speak() string {
    return fmt.Sprintf("I am %s, age %d", a.Name, a.Age)
}

func main() {
    cat := Animal{Name: "Whiskers", Age: 3}
    fmt.Println(cat.Speak())
}
