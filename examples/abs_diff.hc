// Absolute difference of two signed values.
//
// Exercises the subtraction width rule -- i16 - i16 is i17, because the result
// of subtracting two signed values needs one more bit -- and a branch small
// enough that if-conversion should turn it into a multiplexer.
u16 abs_diff(i16 a, i16 b) {
  i17 difference = a - b;
  u16 result = 0;
  if (difference < 0) {
    result = -difference;
  } else {
    result = difference;
  }
  return result;
}
