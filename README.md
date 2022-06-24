# PocketKalc - a C-like expression evaluator

As of june 2022, searching for the term "Calculator" on the front page of github.com **returns about 525,000 results**.
99% of those are just barely functional stub, probably from CS student writing what appears to be their
first assignment. 0.99% are an attempt to mimic the good old pocket calculator from the 1970s, with
at best 4 or 5 operations available for you to use. The remaining 0.01% are usually extensive
programming languages with very rich mathematical function libraries, that require months of
training to be comfortable with.


This program is an attempt to provide a solution that fits right in the middle, while remaining
**simple to use** by providing a relatively **straightforward user-interface**.

To prevent feature creep and keep **complexity under check**, the constraint was to include an inline
help system, which was limited to a single screen. Everything must fit inside a single popup window,
with the same font size than the main user-interface, without any scrollbars, and the information
covering 99% of the features available on a given screen.

Still, this calculator is mostly **oriented toward programmers**. The reason being that it will not
try to hide the quirks of numerical computation done on the CPU: this progrram will show exactly what
is stored in the memory. That's why when you enter `0.1`, it will display `0.10000000000000000555`.
**This is not a bug**, this is a feature, because that's really how the constant 0.1 is stored on
the computer: it cannot be represented exactly (or at least, not with IEEE 754 floating points).

# User-interface

This is more or less what the user-interface looks like, when you start it for the first time:

![Calulator: light and dark theme](https://raw.githubusercontent.com/crystalcrag/WikiResources/main/Calculator.png)

I couldn't decide which theme I liked the most, so I included both.

To use it, simply enter a C-like expression in the edit box at the bottom of the interface, and the
result should appear in the list above. These are the operators that are supported:

| Precedence | Operator | Description | Associativity |
|---|---|---|:---:|
| 15 | ++ | Post-increment | Left-to-right |
| 15 | -- | Post-decrement | " |
| 15 | () | Function call | " |
| 14 | ~  | Bitwise NOT | Right-to-left |
| 13 | !  | Logical NOT | " |
| 13 | \*  | Multiply | Left-to-right |
| 13 | / | Division | " |
| 13 | % | Modulo | " |
| 12 | + | Addition | " |
| 12 | - | Subtraction | " |
| 11 | << | Bitwise left shift | " |
| 11 | >> | Bitwise right shift | " |
| 10 | < | Less than comparion | " |
| 10 | > | Greater than comparion | " |
| 10 | <= | Less or equal than comparion | " |
| 10 | >= | Greater or equal than comparion | " |
| 9 | == | equality comparion | " |
| 9 | != | inequality comparion | " |
| 8 | & | Bitwise AND | " |
| 7 | ^ | Bitwise XOR | " |
| 6 | \| | Bitwise OR | " |
| 5 | && | Logical AND | " |
| 4 | \|\| | Logical OR | " |
| 3 | ?: | Ternary conditional (inline if) | Right-to-left |
| 2 | \*= | Multiply and assign | " |
| 2 | /= | Divide and assign | " |
| 2 | %= | Modulo and assign | " |
| 2 | += | Addition and assign | " |
| 2 | -= | Subtraction and assign | " |
| 2 | <<= | Bitwise left shift and assign | " |
| 2 | >>= | Bitwise right shift and assign | " |
| 2 | &= | Bitwise AND and assign | " |
| 2 | ^= | Bitwise XOR and assign | " |
| 2 | \|= | Bitwise OR and assign | " |
| 1 | , | Comma separator | " |


This precedence table has some **unfortunate choices** that was made decades ago, but kept as-is, so that
you can copy/paste expression from/to C-like language. Most notably are the precedence of the & and |
operator, which have lower priority than the comparison ones. It means that the expression `a & b == 0`
will be evaluated as `a & (b == 0)`, which a bit counter-intuitive.

> If you wonder why this is the case, it dates back from the early days of the C programming language, where the `&&` (logical AND) and `||` (logical OR) didn't exist yet, everything was handled using the `&` and `|` operators. At some point, the authors realized that a logical AND/OR operator would be better suited for the language, but since a lot of code have already been written using the `&` and `|`, they kept their precedence as is.
>
> Interesting how this decision was carried over by some language developed decades later (most notably: Java, Javascript, C# and obviously C++). Fun fact: among the languages that decided to break free from this questionable legacy, there is the Go programming language, where one of its designer was none other than Ken Thomson, co-author of the C programming language (and partly responsible for this decision).


With that little bit of trivia out of the way, expressions can use 5 different **scalar datatypes**:
* **double** : 64bit IEEE 754 floating point.
* **float** : 32bit IEEE 754 floating point.
* **int64_t** : 64bit signed integer.
* **int32_t** : 32bit signed integer.
* **string** : UTF-8 array of characters.

You might be wondering **why include 32bit numbers**, when 64bit scalars are available? The main reason
for including these, is to test integer overflow and/or floating precision loss. 32bit floats are massively
used in the computer graphics world, and knowing their limitation is critical to avoid hard-to-debug
glitches. Likewise, integer overflow and/or bitwise operators on signed number produces sometimes
surprising results.

For example: right shift is a way to divide unsigned integer by a power of two. Applying this on a
signed number is usually undefined behavior, yet it works on the majority of CPU (and GPU), thanks
to the 2-complement representation of negative numbers and sign extension. The result of `-4 >> 1`
will be -2 (as expected). However, due to the same properties, the expression `-1 >> 1` will equal to
`-1` (and not 0 as a more mathematically accurate answer).


Strings, however, have semantics closer to Javascript than C. They are not handled as pointers, but
as immutable objects. You can concatenate strings using the + operator and that's about it. Strings
becomes more useful in the PROG screen (see below).


This calculator also has built-in support for **arrays**, but using them in immediate expressions is not
very useful. Just like strings, they will make more sense in the PROG screen (see below).

# Reusing results

This calculator also has a built-in poor's man version of a **spreadsheet program**.

When you enter an expression and don't assign the result to anything, this calculator will automatically
create a new variable for you. These temporary variables starts with $, followed by whatever number
hasn't been used so far. You can then use these names as a normal variable in any subsequent expression.

It is then possible to **select an expression line** in the list (not to be confused with a result line, 
which is indented by 3 spaces). Once selected, if you modify this line, the corresponding result line
will be updated accordingly. If the variable was used in other expressions, those expression will also
be updated.

This is, in a nutshell, a very primitive spreadsheet program.


# Unit conversion

Another poor's man feature: **conversion between different units**. At the top left corner of the interface,
there is a "UNITS:" label, that show what the default units are for various measurement types. This
calculator has 4 types of measurements:

* Length
* Mass
* Temperature
* Angles

By clicking on the "UNITS" label, you'll be able to select the default unit, that any numbers that have
a unit suffix (displayed below the name of the unit) will be automatically converted to.

For example, if you are a metric user and want to know how tall 5ft4 is, just enter: `5FT+4IN` and it
will display `1.6256m` as a result. Likewise, entering `105degF`, will display `40.5556degC`.

The list of units and category is hardcoded, if you want more, you can create user-defined function
in the "PROG" screen below.

# Graph mode

Besides evaluating expression, you can also draw them over a range of values. The typical use case
for this screen is to find a best-fit curve that matches some properties without having to deal with
some pesky equations. In some way, it is a poor's man version of the https://www.desmos.com/calculator
but, obviously, **way more** primitive.

Typical use case: find a curve that have a gaussian shape, centered around 0, that goes from 1 to 1.2 on
the Y axis and -0.1 and 0.1 on the X axis.

The gaussian formula is: `Amplitude * exp(- (x - shiftX)^2 / (2 * rangeX))+ShiftY`

Some terms are obvious: `ShiftX = 0, ShiftY = 1, Amplitude = 0.2`.

If you enter the formula: `0.2 * exp(- X*X / 2) + 1`

You'll see that the range on X axis is way too large. By playing with the rangeX parameter, you find
that this formula is close enough: `0.2*exp(-x*x/0.005)+1`

And voila: no need for complicated equation solving, just fiddling with some parameters.

# Program mode

Finally, this calculator has a poor's man **programming language** integrated. The typical use case for
this is a add user-defined functions, that can be used from the other screens (EXPR/GRAPH).

Since this language is Turing-complete, you can end up writing programs that can take a long time to
terminate (including never). Don't you worry! This calculator has built-in mechanism to prevent program
from eating too much RAM and or CPU (by being able to forcibly kill any running program at any time).



