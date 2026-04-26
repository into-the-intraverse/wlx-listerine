; inherits: cpp

; Main Unreal reflection macros — keyword.directive distinguishes them from
; regular function calls. The cpp inherited queries treat these as plain function calls;
; this override re-tags them via structural capture of the upstream grammar's dedicated nodes.
((uclass_macro) @keyword.directive)
((ustruct_macro) @keyword.directive)
((uenum_macro) @keyword.directive)
((uproperty_macro) @keyword.directive)
((ufunction_macro) @keyword.directive)
((umeta_macro) @keyword.directive)

; GENERATED_BODY and variants — captured via unreal_body_macro node
((unreal_body_macro) @keyword.directive)

; Delegate declaration macros — caught by unreal_declaration_macro
((unreal_declaration_macro) @keyword.directive)

; PROJECTNAME_API export macros (e.g. MYGAME_API, MYPLUGIN_API).
; unreal_api_specifier is a token node in the upstream grammar.
; We capture it as @attribute so themes can render it like Rust attributes / C# annotations.
((unreal_api_specifier) @attribute)

; Common Unreal property/function specifiers found inside UPROPERTY()/UFUNCTION() args.
; The upstream grammar provides dedicated unreal_specifier_keyword nodes,
; so we use structural capture instead of text-based identifier matching.
; This is more robust than the identifier-match approach.
((unreal_specifier_keyword) @attribute.builtin)

; Fallback: if specifiers appear as plain identifiers in DECLARE_* macros or elsewhere,
; match them as text. This handles edge cases where the upstream parser doesn't have
; a specialized node, or where user code uses unknown specifier names.
((identifier) @attribute.builtin
 (#match? @attribute.builtin
  "^(BlueprintCallable|BlueprintReadOnly|BlueprintReadWrite|BlueprintImplementableEvent|BlueprintNativeEvent|BlueprintPure|EditAnywhere|EditDefaultsOnly|EditInstanceOnly|VisibleAnywhere|VisibleDefaultsOnly|VisibleInstanceOnly|Category|Replicated|ReplicatedUsing|Transient|SaveGame|Config|GlobalConfig|Localized|SkipSerialization|Meta|ClassGroup|HideCategories|ShowCategories|Within|Blueprintable|NotBlueprintable|MinimalAPI|customConstructor|noexport|placeable|notplaceable|hidedropdown|abstract|Abstract|Deprecated)$"))
