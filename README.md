## what is goo

- fully interpreted, stack based language
- some instructions: pushnum, pushvar, popvar, add, print, etc.
- if and while statements

## example program(s)

<details>
<summary>3n+1 thing</summary>

```
pushstr "type nuber: "
print

readnum
popvar n

defsub even
	pushstr "even\n"
	print

	pushvar n
	pushnum 2
	div
	popvar n

	return
endsub

defsub odd
	pushstr "odd\n"
	print

	pushnum 1
	pushvar n
	pushnum 3
	mul
	add
	popvar n

	return
endsub

pushnum 0
popvar iter

while
	pushvar n
	pushnum 1
	neq
do
	pushvar n
	print
	pushstr "\n"
	print

	if
		pushvar n
		pushnum 2
		mod
	then
		gosub odd
	else
		gosub even
	endif

	if
		pushvar n
		pushnum 1677216
		gte
	then
		pushstr "too big\n"
		print
		break
	endif

	pushvar iter
	pushnum 1
	add
	popvar iter
endwhile

pushstr "iter="
print
pushvar iter
print
pushstr "\n"
print
```
</details>

## todo
- fix TODO comments
- ~~subroutine reading doesnt really need to go on the block stack, make the subroutine state its own thing~~
- make nice functions
	- `is_reading_subroutine()`
	- `goto(int loc)`
- better error stuff. not just `printf("gah! info"); exit(1);`
	- stack trace
- reorganize stuff
- factor out " \t" into DELIM or smth
- imply `return` before `endsub`?
- working comments
- different number type? like arbitrary precision
- label & goto
- apply the math token optimization (where `char *tok` is a pointer to the keywords array instead of the code for faster checking) to other cases
- keep track of what pushed each value onto the stack for debuggering

## how things work
- variables:
	- stored as a list of variable names and another for values
	- on `pushvar`, check if the variable exists and push the value
	- on `popvar`, pop a value into the variable (creates a new one if it doesn't exist)
- skipping:
	- there's a special state of skipping where lines aren't executed
	- used, for example, when skipping to an `endwhile` after the condition is false
	- you can't just skip to the next `endif`/`endwhile` bc of nested blocks, so `skip_depth` is used to keep track of nested blocks of the same type
	- note that the same token can do different things depending on if its skipping or not (ex. `else`)
	- `do_skip` behavior depends on the current block:
		- `while`:
			- another `while` increments the depth
			- an `endwhile` decrements the depth
		- `if`:
			- another `if` increments the depth
			- a relevant `else` will stop skipping
				- "relevant" meaning its part of the current `if`, not a nested one. checked with `skip_depth == 0`
				- it stops skipping bc we know that the condition was false bc it has been skipping (otherwise `do_skip` wouldn't have been called), so it should start running the following code
			- an `endif` decrements the depth
	- if `skip_depth` is negative, skipping stops and a block is popped
- if:
	- eval condition
	- if true:
		- continue to `endif` or `else`, once at `else` skip to `endif`
	- if false:
		- skip to `else` or `endif`
- while:
	- eval condition
	- if true, continue
	- if false, skip to `endwhile`
	- if not skipping, at `endwhile`, jump to the condition (one after `block.while_start`)
	- break:
		- pop from block stack until the top is `while`
		- skip to `endwhile`
- subroutines:
	- on `defsub <name>`:
		- register start of subroutine
		- skip until `endsub`
	- needs `return` before `endsub`!
	- on `gosub <name>`, the `lineptr` is pushed to the call stack and it jumps to the start of the subroutine
