#include "GameFramework/Actor.h"

UCLASS()
class MYGAME_API AMyActor : public AActor {
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable)
    void DoSomething();
};
