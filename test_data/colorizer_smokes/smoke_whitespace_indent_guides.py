def example():
	x = 1                                  # tab-indented; trailing spaces above
	if x:
		if x > 0:
			if x > 1:
				return "deep nesting"   # 4 levels of indent for guide rendering
	return None
