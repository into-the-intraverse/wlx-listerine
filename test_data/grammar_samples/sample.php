<?php
// PHP sample
namespace App;

class Greeter {
    private string $prefix;

    public function __construct(string $prefix = "Hello") {
        $this->prefix = $prefix;
    }

    public function greet(string $name): string {
        return "{$this->prefix}, {$name}!";
    }
}

$g = new Greeter();
echo $g->greet("World") . "\n";
