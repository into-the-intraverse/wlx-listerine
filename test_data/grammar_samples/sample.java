// Java sample
import java.util.List;
import java.util.ArrayList;

public class Sample {
    private final String name;

    public Sample(String name) {
        this.name = name;
    }

    public static void main(String[] args) {
        List<Integer> numbers = new ArrayList<>();
        numbers.add(42);
        System.out.println("Count: " + numbers.size());
    }
}
