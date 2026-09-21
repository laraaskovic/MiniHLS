#include "interp/interpreter.hpp"

namespace minihls {

// ---------------------------------------------------------------- memory
// LANGUAGE.md's runtime table for arrays lives in these two functions.

Bits Interpreter::loadElem(const Symbol* s, const Bits& index) {
  auto& arr = arrays_[s];
  u128 i = index.raw;                     // indices are unsigned per the spec
  if (i >= arr.size())                    // out of range -> 0
    return Bits::make(0, s->type.width, s->type.isSigned);
  return arr[static_cast<size_t>(i)];
}

void Interpreter::storeElem(const Symbol* s, const Bits& index, Bits v) {
  auto& arr = arrays_[s];
  u128 i = index.raw;
  if (i >= arr.size()) return;            // out of range -> no effect
  arr[static_cast<size_t>(i)] = coerce(v, s->type);
}

Bits Interpreter::readStream(const Symbol* s, Range where) {
  auto& in = streamIn_[s];
  size_t& pos = streamPos_[s];
  if (pos >= in.size()) {
    diags_.error(where, "stream '" + s->name + "' has no more elements");
    failed_ = true;
    return Bits::make(0, s->type.width, s->type.isSigned);
  }
  return in[pos++];
}

// ----------------------------------------------------------- expressions

Bits Interpreter::eval(Expr& e) {
  switch (e.kind) {

  case ExprKind::IntLit: {
    auto& n = static_cast<IntLit&>(e);
    return Bits::make(n.value, e.type.width, e.type.isSigned);
  }

  case ExprKind::NameRef:
    return scalars_[static_cast<NameRef&>(e).symbol];

  case ExprKind::Index: {
    auto& n = static_cast<Index&>(e);
    return loadElem(n.symbol, eval(*n.index));
  }

  case ExprKind::Cast: {
    auto& n = static_cast<Cast&>(e);
    return castTo(eval(*n.operand), n.target.width, n.target.isSigned);
  }

  case ExprKind::Unary: {
    auto& n = static_cast<Unary&>(e);
    Bits a = eval(*n.operand);
    switch (n.op) {
      case Tok::Minus: return neg(a);
      case Tok::Plus:  return a;
      case Tok::Tilde: return bitNot(a);
      case Tok::Bang:  return Bits::make(a.raw == 0 ? 1 : 0, 1, false);
      default:         return a;
    }
  }

  case ExprKind::Binary: {
    auto& n = static_cast<Binary&>(e);
    Bits l = eval(*n.lhs), r = eval(*n.rhs);
    switch (n.op) {
      case Tok::Plus:    return add(l, r);
      case Tok::Minus:   return sub(l, r);
      case Tok::Star:    return mul(l, r);
      case Tok::Slash:   return divide(l, r);
      case Tok::Percent: return remainder(l, r);
      case Tok::Amp:     return bitAnd(l, r);
      case Tok::Pipe:    return bitOr(l, r);
      case Tok::Caret:   return bitXor(l, r);
      case Tok::Shl:     return shl(l, r);
      case Tok::Shr:     return shr(l, r);
      case Tok::Lt:      return compare(l, r, Cmp::Lt);
      case Tok::Le:      return compare(l, r, Cmp::Le);
      case Tok::Gt:      return compare(l, r, Cmp::Gt);
      case Tok::Ge:      return compare(l, r, Cmp::Ge);
      case Tok::EqEq:    return compare(l, r, Cmp::Eq);
      case Tok::BangEq:  return compare(l, r, Cmp::Ne);
      // Both operands are u1 and neither short-circuits — see below.
      case Tok::AmpAmp:  return Bits::make((l.raw && r.raw) ? 1 : 0, 1, false);
      case Tok::PipePipe:return Bits::make((l.raw || r.raw) ? 1 : 0, 1, false);
      default:           return Bits{};
    }
  }

  case ExprKind::Ternary: {
    auto& n = static_cast<Ternary&>(e);
    Bits c = eval(*n.cond);
    // LANGUAGE.md: NOTHING short-circuits. Both arms are evaluated even
    // though only one is selected, because in hardware both are wires into
    // a mux and are computed regardless. Doing this the C way would make
    // the interpreter disagree with the RTL — and this file is the thing
    // the RTL gets checked against.
    Bits t = eval(*n.thenE);
    Bits f = eval(*n.elseE);
    Bits picked = c.raw ? t : f;
    return coerce(picked, e.type);        // result width is max of the arms
  }
  }
  return Bits{};
}

// ------------------------------------------------------------ statements

void Interpreter::execBlock(Block& b) {
  for (auto& s : b.stmts) {
    if (failed_) return;
    exec(*s);
  }
}

void Interpreter::exec(Stmt& s) {
  switch (s.kind) {

  case StmtKind::VarDecl: {
    auto& n = static_cast<VarDecl&>(s);
    Bits v = n.initIsRead ? readStream(n.readSymbol, n.range)
                          : eval(*n.init);
    // S2 proved this is a legal widening; coerce makes it explicit.
    scalars_[n.symbol] = coerce(v, n.type);
    return;
  }

  case StmtKind::ArrayDecl: {
    auto& n = static_cast<ArrayDecl&>(s);
    auto& arr = arrays_[n.symbol];
    arr.assign(n.symbol->arrayLength,                    // S3 folded the size
               Bits::make(0, n.elem.width, n.elem.isSigned));  // unwritten -> 0
    for (size_t i = 0; i < n.init.size() && i < arr.size(); ++i)
      arr[i] = coerce(eval(*n.init[i]), n.elem);
    return;
  }

  case StmtKind::Assign: {
    auto& n = static_cast<Assign&>(s);
    Bits v = n.valueIsRead ? readStream(n.readSymbol, n.range)
                           : eval(*n.value);
    if (n.index) storeElem(n.symbol, eval(*n.index), v);
    else         scalars_[n.symbol] = coerce(v, n.symbol->type);
    return;
  }

  case StmtKind::Write: {
    auto& n = static_cast<Write&>(s);
    streamOut_[n.symbol].push_back(coerce(eval(*n.value), n.symbol->type));
    return;
  }

  case StmtKind::If: {
    auto& n = static_cast<If&>(s);
    // Here we DO branch — only one arm runs. That is not the same as `?:`,
    // which computes both. In E8 the difference is concrete: `?:` becomes a
    // mux, `if` becomes a choice of FSM state.
    if (eval(*n.cond).raw) execBlock(*n.thenB);
    else if (n.elseS)      exec(*n.elseS);
    return;
  }

  case StmtKind::For: {
    auto& n = static_cast<For&>(s);
    Bits iv   = coerce(eval(*n.init), n.ivType);
    Bits step = coerce(eval(*n.step), n.ivType);

    // S3 computed the exact trip count. We do NOT re-test the condition each
    // iteration: the count is a compile-time fact, which is the whole reason
    // the language restricts the loop header — and it is what lets E8 build
    // a counter instead of evaluating a comparison in hardware every cycle.
    for (uint64_t k = 0; k < n.tripCount; ++k) {
      if (failed_) return;
      scalars_[n.ivSymbol] = iv;
      execBlock(*n.body);
      // The induction update is exempt from the width-growth rules; S3
      // already proved every value it takes fits its declared type.
      u128 next = n.stepIsAdd ? (iv.raw + step.raw) : (iv.raw - step.raw);
      iv = Bits::make(next, n.ivType.width, n.ivType.isSigned);
    }
    return;
  }

  case StmtKind::Return: {
    auto& n = static_cast<Return&>(s);
    returnValue_ = eval(*n.value);
    return;
  }

  case StmtKind::Block:
    execBlock(static_cast<Block&>(s));
    return;
  }
}

// ----------------------------------------------------------- entry point

std::optional<RunResult> Interpreter::run(Program& program,
                                          const std::vector<ParamValue>& inputs) {
  scalars_.clear(); arrays_.clear();
  streamIn_.clear(); streamPos_.clear(); streamOut_.clear();
  failed_ = false;

  // File-scope constants, folded by S3.
  for (ConstDecl& c : program.consts) {
    if (c.isArray) {
      auto& arr = arrays_[c.symbol];
      for (const Bits& v : c.foldedElements) arr.push_back(coerce(v, c.type));
    } else {
      if (c.valueKnown) scalars_[c.symbol] = coerce(c.value, c.type);
    }
  }

  const auto& params = program.fn.params;
  if (inputs.size() != params.size()) {
    diags_.error(program.fn.range,
                 "expected " + std::to_string(params.size()) + " arguments, got " +
                 std::to_string(inputs.size()));
    return std::nullopt;
  }

  std::vector<const Symbol*> outStreams;   // in parameter order

  for (size_t i = 0; i < params.size(); ++i) {
    const Param& p = params[i];
    const Symbol* sym = p.symbol;
    switch (p.kind) {
      case ParamKind::Scalar:
        scalars_[sym] = coerce(inputs[i].scalar, p.type);
        break;
      case ParamKind::Array: {
        auto& arr = arrays_[sym];
        arr.assign(sym->arrayLength, Bits::make(0, p.type.width, p.type.isSigned));
        for (size_t k = 0; k < inputs[i].elements.size() && k < arr.size(); ++k)
          arr[k] = coerce(inputs[i].elements[k], p.type);
        break;
      }
      case ParamKind::StreamIn: {
        auto& in = streamIn_[sym];
        for (const Bits& v : inputs[i].elements) in.push_back(coerce(v, p.type));
        streamPos_[sym] = 0;
        break;
      }
      case ParamKind::StreamOut:
        streamOut_[sym];                   // create the (empty) vector
        outStreams.push_back(sym);
        break;
    }
  }

  execBlock(*program.fn.body);
  if (failed_) return std::nullopt;

  RunResult result;
  result.returnValue = coerce(returnValue_, program.fn.returnType);
  for (const Symbol* sym : outStreams)
    result.streamOutputs.push_back(streamOut_[sym]);
  return result;
}

} // namespace minihls