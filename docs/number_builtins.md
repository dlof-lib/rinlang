# Rin Number Built-ins

Rin provides a compact native Number toolkit in the runtime.

| Function | Purpose |
|---|---|
| `abs`, `sqrt`, `pow` | basic numeric math |
| `floor`, `ceil`, `round`, `trunc`, `frac` | integer/fraction handling |
| `min`, `max`, `clamp` | bounds |
| `sign`, `isEven`, `isOdd`, `isInteger` | numeric classification |
| `isFinite`, `isNaN` | floating-point classification |
| `mod` | floating remainder |
| `gcd`, `lcm` | integer arithmetic |
| `factorial`, `isPrime` | integer math |
| `hypot`, `root` | geometry/root math |
| `degrees`, `radians` | angle conversion |
| `mapRange` | map one numeric range to another |
| `approx` | tolerance comparison |
| `sumRange` | inclusive integer range sum |
| `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2` | trigonometry |
| `exp`, `log`, `log10`, `ln`, `cbrt` | exponential/logarithmic math |
| `lerp` | linear interpolation |
| `random`, `seed` | deterministic/random generation |

All numeric arguments are validated by the runtime. Invalid domains produce Rin diagnostics instead of silently returning a bad result.
