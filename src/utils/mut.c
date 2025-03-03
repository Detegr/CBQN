#include "../core.h"
#include "mut.h"

FORCE_INLINE void mut_init(Mut* m, u8 n) {
  m->fns = &mutFns[n];
  usz ia = m->ia;
  u64 sz;
  // hack around inlining of the allocator too many times
  switch(n) { default: UD;
    case el_bit:              sz = BITARR_SZ(   ia); break;
    case el_i8:  case el_c8:  sz = TYARR_SZ(I8, ia); break;
    case el_i16: case el_c16: sz = TYARR_SZ(I16,ia); break;
    case el_i32: case el_c32: sz = TYARR_SZ(I32,ia); break;
    case el_f64:              sz = TYARR_SZ(F64,ia); break;
    case el_B:;
      HArr_p t = m_harrUp(ia);
      m->val = (Arr*)t.c;
      m->a = t.c->a;
      return;
  }
  Arr* a = m_arr(sz, m->fns->valType, ia);
  m->val = a;
  m->a = ((TyArr*)a)->a;
}

#if __clang__
NOINLINE void make_mut_init(Mut* rp, ux ia, u8 el) {
  MAKE_MUT(r, ia)
  mut_init(r, el);
  *rp = r_val;
}
#else
NOINLINE Mut make_mut_init(ux ia, u8 el) {
  MAKE_MUT(r, ia)
  mut_init(r, el);
  return r_val;
}
#endif

static Arr* (*cpyFns[])(B) = {
  [el_bit] = cpyBitArr,
  [el_i8]  = cpyI8Arr,  [el_c8]  = cpyC8Arr,
  [el_i16] = cpyI16Arr, [el_c16] = cpyC16Arr,
  [el_i32] = cpyI32Arr, [el_c32] = cpyC32Arr,
  [el_f64] = cpyF64Arr, [el_B]   = cpyHArr
};
NOINLINE void mut_to(Mut* m, u8 n) {
  u8 o = m->fns->elType;
  assert(o!=el_B);
  if (o==el_MAX) {
    mut_init(m, n);
  } else {
    m->fns = &mutFns[n];
    SPRNK(m->val, 1);
    m->val->sh = &m->val->ia;
    #ifdef USE_VALGRIND
      VALGRIND_MAKE_MEM_DEFINED(m->val, mm_size((Value*)m->val)); // it's incomplete, but it's a typed array so garbage is acceptable
    #endif
    #ifdef DEBUG
      if (n==el_B && o==el_f64) { // hack to make toHArr calling f64arr_get not cry about possible sNaN floats
        usz ia = m->val->ia;
        f64* p = f64arr_ptr(taga(m->val));
        for (usz i = 0; i < ia; i++) if (!isF64(b(p[i]))) p[i] = 1.2217638442043777e161; // 0x6161616161616161
      }
    #endif
    Arr* t = cpyFns[n](taga(m->val));
    m->val = t;
    m->a = n==el_B? (void*)((HArr*)t)->a : (void*)((TyArr*)t)->a;
  }
}

NOINLINE B vec_addF(B w, B x) {
  usz wia = IA(w);
  MAKE_MUT_INIT(r, wia+1, el_or(TI(w,elType), selfElType(x))); MUTG_INIT(r);
  mut_copyG(r, 0, w, 0, wia);
  mut_setG(r, wia, x);
  dec(w);
  return mut_fv(r);
}
NOINLINE B vec_addN(B w, B x) {
  return vec_add(w, x);
}

NOINLINE void mut_pfree(Mut* m, usz n) { // free the first n elements
  if (m->fns->elType==el_B) harr_pfree(taga(m->val), n);
  else mm_free((Value*) m->val);
}


B vec_join(B w, B x) {
  bool unused;
  return arr_join_inline(w, x, true, &unused);
}



#define CHK(REQ,NEL,N,...) if (!(REQ)) mut_to(m, NEL); m->fns->m_##N##G(m, __VA_ARGS__);
#define CHK_S(REQ,X,N,...) CHK(REQ, el_or(m->fns->elType, selfElType(X)), N, __VA_ARGS__)


#define DEF0(RT, N, TY, INIT, CHK, EL, ARGS, ...) \
  static RT m_##N##G_##TY ARGS;            \
  static RT m_##N##_##TY ARGS { INIT       \
    Mut* m = a;                            \
    if (RARE(!(CHK))) {                    \
      mut_to(m, EL);                       \
      m->fns->m_##N##G(m->a, __VA_ARGS__); \
    } else {                               \
      m_##N##G_##TY(m->a, __VA_ARGS__);    \
    }                                      \
  }

#define DEF(RT, N, TY, INIT, CHK, EL, ARGS, ...) \
  DEF0(RT, N, TY, INIT, CHK, EL, ARGS, __VA_ARGS__) \
  static RT m_##N##G_##TY ARGS

#define DEF_S( RT, N, TY, CHK, X, ARGS, ...) DEF(RT, N, TY, , CHK, el_or(el_##TY, selfElType(X)), ARGS, __VA_ARGS__)
#define DEF_E( RT, N, TY, CHK, X, ARGS, ...) DEF(RT, N, TY, , CHK, TI(X,elType),  ARGS, __VA_ARGS__)
#define DEF_G( RT, N, TY,         ARGS, ...)  \
  static RT m_##N##G_##TY ARGS;               \
  static RT m_##N##_##TY ARGS {               \
    m_##N##G_##TY(((Mut*)a)->a, __VA_ARGS__); \
  }                                           \
  static RT m_##N##G_##TY ARGS

DEF_S(void, set, MAX, false,    x, (void* a, usz ms, B x), ms, x) { err("m_setG_MAX"); }
DEF_S(void, set, bit, q_bit(x), x, (void* a, usz ms, B x), ms, x) { bitp_set((u64*)a, ms, o2bG(x)); }
DEF_S(void, set, i8,  q_i8 (x), x, (void* a, usz ms, B x), ms, x) { (( i8*)a)[ms] = o2iG(x); }
DEF_S(void, set, i16, q_i16(x), x, (void* a, usz ms, B x), ms, x) { ((i16*)a)[ms] = o2iG(x); }
DEF_S(void, set, i32, q_i32(x), x, (void* a, usz ms, B x), ms, x) { ((i32*)a)[ms] = o2iG(x); }
DEF_S(void, set, c8,  q_c8 (x), x, (void* a, usz ms, B x), ms, x) { (( u8*)a)[ms] = o2cG(x); }
DEF_S(void, set, c16, q_c16(x), x, (void* a, usz ms, B x), ms, x) { ((u16*)a)[ms] = o2cG(x); }
DEF_S(void, set, c32, q_c32(x), x, (void* a, usz ms, B x), ms, x) { ((u32*)a)[ms] = o2cG(x); }
DEF_S(void, set, f64, q_f64(x), x, (void* a, usz ms, B x), ms, x) { ((f64*)a)[ms] = o2fG(x); }
DEF_G(void, set, B,                (void* a, usz ms, B x), ms, x) { ((  B*)a)[ms] = x; }

DEF_S(void, fill, MAX, false,    x, (void* a, usz ms, B x, usz l), ms, x, l) { err("m_fillG_MAX"); }
DEF_S(void, fill, bit, q_bit(x), x, (void* a, usz ms, B x, usz l), ms, x, l) { u64* p = (u64*)a; bool v = o2bG(x);
  usz me = ms+l;
  if (ms>>6 == me>>6) {
    u64 m = ((1ULL<<(me&63)) - (1ULL<<(ms&63)));
    if (v) p[ms>>6]|=  m;
    else   p[ms>>6]&= ~m;
  } else {
    u64 vx = bitx(x);
    u64 m0 = (1ULL<<(ms&63)) - 1;
    if (v) p[ms>>6]|= ~m0;
    else   p[ms>>6]&=  m0;
    for (usz i = (ms>>6)+1; i < (me>>6); i++) p[i] = vx;
    u64 m1 = (1ULL<<(me&63)) - 1;
    if (v) p[me>>6]|=  m1;
    else   p[me>>6]&= ~m1;
  }
}
DEF_S(void, fill, i8 , q_i8 (x), x, (void* a, usz ms, B x, usz l), ms, x, l) { i8*  p = ms+( i8*)a; i8  v = o2iG(x); for (usz i = 0; i < l; i++) p[i] = v; }
DEF_S(void, fill, i16, q_i16(x), x, (void* a, usz ms, B x, usz l), ms, x, l) { i16* p = ms+(i16*)a; i16 v = o2iG(x); for (usz i = 0; i < l; i++) p[i] = v; }
DEF_S(void, fill, i32, q_i32(x), x, (void* a, usz ms, B x, usz l), ms, x, l) { i32* p = ms+(i32*)a; i32 v = o2iG(x); for (usz i = 0; i < l; i++) p[i] = v; }
DEF_S(void, fill, c8 , q_c8 (x), x, (void* a, usz ms, B x, usz l), ms, x, l) { u8*  p = ms+( u8*)a; u8  v = o2cG(x); for (usz i = 0; i < l; i++) p[i] = v; }
DEF_S(void, fill, c16, q_c16(x), x, (void* a, usz ms, B x, usz l), ms, x, l) { u16* p = ms+(u16*)a; u16 v = o2cG(x); for (usz i = 0; i < l; i++) p[i] = v; }
DEF_S(void, fill, c32, q_c32(x), x, (void* a, usz ms, B x, usz l), ms, x, l) { u32* p = ms+(u32*)a; u32 v = o2cG(x); for (usz i = 0; i < l; i++) p[i] = v; }
DEF_S(void, fill, f64, isF64(x), x, (void* a, usz ms, B x, usz l), ms, x, l) { f64* p = ms+(f64*)a; f64 v = o2fG(x); for (usz i = 0; i < l; i++) p[i] = v; }
DEF_G(void, fill, B  ,              (void* a, usz ms, B x, usz l), ms, x, l) {
  B* p = ms + (B*)a;
  for (usz i = 0; i < l; i++) p[i] = x;
  if (isVal(x)) incBy(x, l);
  return;
}


#if SINGELI_SIMD
  #define DEF_COPY(T, BODY) DEF0(void, copy, T, u8 xe=TI(x,elType); u8 ne=el_or(xe,el_##T);, ne==el_##T, ne, (void* a, usz ms, B x, usz xs, usz l), ms, x, xs, l)
#else
  #define DEF_COPY(T, BODY)  DEF(void, copy, T, u8 xe=TI(x,elType); u8 ne=el_or(xe,el_##T);, ne==el_##T, ne, (void* a, usz ms, B x, usz xs, usz l), ms, x, xs, l) { u8 xt=TY(x); (void)xt; BODY }
#endif
#define BIT_COPY(T) for (usz i = 0; i < l; i++) rp[i] = bitp_get(xp, xs+i); return;
#define PTR_COPY(X, R) for (usz i = 0; i < l; i++) ((R*)rp)[i] = ((X*)xp)[i+xs]; return;

void bit_cpyN(u64* r, usz rs, u64* x, usz xs, usz l) {
  bit_cpy(r, rs, x, xs, l);
}

DEF_COPY(bit, { bit_cpyN((u64*)a, ms, bitarr_ptr(x), xs, l); return; })
DEF_COPY(i8 , { i8*  rp = ms+(i8*)a; void* xp = tyany_ptr(x);
  switch (xt) { default: UD;
    case t_bitarr:                  BIT_COPY(i8)
    case t_i8arr: case t_i8slice:   PTR_COPY(i8, i8)
  }
})
DEF_COPY(i16, { i16* rp = ms+(i16*)a; void* xp = tyany_ptr(x);
  switch (xt) { default: UD;
    case t_bitarr:                  BIT_COPY(i16)
    case t_i8arr:  case t_i8slice:  PTR_COPY(i8, i16)
    case t_i16arr: case t_i16slice: PTR_COPY(i16, i16)
  }
})
DEF_COPY(i32, { i32* rp = ms+(i32*)a; void* xp = tyany_ptr(x);
  switch (xt) { default: UD;
    case t_bitarr:                  BIT_COPY(i32)
    case t_i8arr:  case t_i8slice:  PTR_COPY(i8, i32)
    case t_i16arr: case t_i16slice: PTR_COPY(i16, i32)
    case t_i32arr: case t_i32slice: PTR_COPY(i32, i32)
  }
})
DEF_COPY(f64, { f64* rp = ms+(f64*)a; void* xp = tyany_ptr(x);
  switch (xt) { default: UD;
    case t_bitarr:                  BIT_COPY(f64)
    case t_i8arr:  case t_i8slice:  PTR_COPY(i8, f64)
    case t_i16arr: case t_i16slice: PTR_COPY(i16, f64)
    case t_i32arr: case t_i32slice: PTR_COPY(i32, f64)
    case t_f64arr: case t_f64slice: PTR_COPY(f64, f64)
  }
})
DEF_COPY(c8 , { u8*  rp = ms+(u8*)a; void* xp = tyany_ptr(x); PTR_COPY(u8, u8) })
DEF_COPY(c16, { u16* rp = ms+(u16*)a; void* xp = tyany_ptr(x);
  switch (xt) { default: UD;
    case t_c8arr:  case t_c8slice:  PTR_COPY(u8, u16)
    case t_c16arr: case t_c16slice: PTR_COPY(u16, u16)
  }
})
DEF_COPY(c32, { u32* rp = ms+(u32*)a; void* xp = tyany_ptr(x);
  switch (xt) { default: UD;
    case t_c8arr:  case t_c8slice:  PTR_COPY(u8, u32)
    case t_c16arr: case t_c16slice: PTR_COPY(u16, u32)
    case t_c32arr: case t_c32slice: PTR_COPY(u32, u32)
  }
})
DEF_E(void, copy, MAX, false, x, (void* a, usz ms, B x, usz xs, usz l), ms, x, xs, l) { err("m_copyG_MAX"); }
NOINLINE void m_copyG_B_generic(void* a, B* mpo, B x, usz xs, usz l) {
  SLOW1("copyG", x);
  SGet(x)
  for (usz i = 0; i < l; i++) mpo[i] = Get(x,i+xs);
}
DEF_G(void, copy, B,             (void* a, usz ms, B x, usz xs, usz l), ms, x, xs, l) {
  B* mpo = ms+(B*)a;
  switch(TY(x)) {
    case t_bitarr: { u64* xp = bitarr_ptr(x); for (usz i = 0; i < l; i++) mpo[i] = m_i32(bitp_get(xp, xs+i)); return; }
    case t_i8arr:  case t_i8slice:  { i8*  xp = i8any_ptr (x); for (usz i = 0; i < l; i++) mpo[i] = m_i32(xp[i+xs]); return; }
    case t_i16arr: case t_i16slice: { i16* xp = i16any_ptr(x); for (usz i = 0; i < l; i++) mpo[i] = m_i32(xp[i+xs]); return; }
    case t_i32arr: case t_i32slice: { i32* xp = i32any_ptr(x); for (usz i = 0; i < l; i++) mpo[i] = m_i32(xp[i+xs]); return; }
    case t_c8arr:  case t_c8slice:  { u8*  xp = c8any_ptr (x); for (usz i = 0; i < l; i++) mpo[i] = m_c32(xp[i+xs]); return; }
    case t_c16arr: case t_c16slice: { u16* xp = c16any_ptr(x); for (usz i = 0; i < l; i++) mpo[i] = m_c32(xp[i+xs]); return; }
    case t_c32arr: case t_c32slice: { u32* xp = c32any_ptr(x); for (usz i = 0; i < l; i++) mpo[i] = m_c32(xp[i+xs]); return; }
    case t_harr: case t_hslice: case t_fillarr: case t_fillslice:;
      B* xp = arr_bptr(x)+xs;
      for (usz i = 0; i < l; i++) inc(xp[i]);
      memcpy(mpo, xp, l*sizeof(B));
      return;
    case t_f64arr: case t_f64slice:
      assert(sizeof(B)==sizeof(f64));
      memcpy(mpo, f64any_ptr(x)+xs, l*sizeof(B));
      return;
    default: {
      m_copyG_B_generic(a, mpo, x, xs, l);
      return;
    }
  }
}

#if SINGELI_SIMD
  static u64 loadu_u64(u64* p) { u64 v; memcpy(&v, p, 8); return v; }
  #define SINGELI_FILE copy
  #include "./includeSingeli.h"
  typedef void (*copy_fn)(void*, void*, u64, void*);
  
  static void badCopy(void* xp, void* rp, u64 len, void* xRaw) {
    err("Copying wrong array type");
  }
  
  #define COPY_FN(X,R) simd_copy_##X##_##R
  #define MAKE_CPY(TY, MAKE, GET, WR, XRP, H2T, T, ...) \
  static copy_fn copy##T##Fns[10];                \
  NOINLINE void cpy##T##Arr_BF(void* xp, void* rp, u64 ia, Arr* xa) { \
    AS2B fn = TIv(xa,GET);                 \
    for (usz i=0; i<ia; i++) WR(fn(xa,i)); \
  } \
  static void cpy##T##Arr_B(void* xp, void* rp, u64 ia, void* xRaw) { \
    Arr* xa = (Arr*)xRaw; B* bxp = arrV_bptr(xa); \
    if (bxp!=NULL && sizeof(B)==sizeof(f64)) H2T; \
    else cpy##T##Arr_BF(xp, rp, ia, xa);          \
  }                                               \
  static copy_fn copy##T##Fns[] = __VA_ARGS__;    \
  Arr* cpy##T##Arr(B x) {   \
    usz ia = IA(x);         \
    MAKE; arr_shCopy(r, x); \
    if (ia>0) {             \
      copy##T##Fns[TI(x,elType)](tyany_ptr(x), XRP, ia, a(x)); \
    }                       \
    if (TY) ptr_decT(a(x)); \
    else decG(x);           \
    NOGC_E; return (Arr*)r; \
  }
  #define BIT_PUT(V) bitp_set((u64*)rp, i, o2bG(V))
  #define H2T_COPY(T) copy##T##Fns[el_MAX](bxp, rp, ia, xRaw)
  #define MAKE_TYCPY(T, E, F, ...) MAKE_CPY(1, T##Atom* rp; Arr* r = m_##E##arrp(&rp, ia), getU, ((T##Atom*)rp)[i] = F, rp, H2T_COPY(T), T, __VA_ARGS__)
  #define MAKE_CCPY(T,E) MAKE_TYCPY(T, E, o2cG, {badCopy,     badCopy,      badCopy,       badCopy,       badCopy,       COPY_FN(c8,E),COPY_FN(c16,E),COPY_FN(c32,E),cpy##T##Arr_B,COPY_FN(B,E)})
  #define MAKE_ICPY(T,E) MAKE_TYCPY(T, E, o2fG, {COPY_FN(1,E),COPY_FN(i8,E),COPY_FN(i16,E),COPY_FN(i32,E),COPY_FN(f64,E),badCopy,      badCopy,       badCopy,       cpy##T##Arr_B,COPY_FN(f64,E)})
  MAKE_CPY(0, HArr_p p = m_harrUp(ia); Arr* r = (Arr*)p.c, get, ((B*)rp)[i] =, p.a, for (usz i=0; i<ia; i++) ((B*)rp)[i] = inc(bxp[i]),
                                    H,          {COPY_FN(1,B),COPY_FN(i8,B),COPY_FN(i16,B),COPY_FN(i32,B),COPY_FN(f64,B),COPY_FN(c8,B),COPY_FN(c16,B),COPY_FN(c32,B),cpyHArr_B,    COPY_FN(f64,B)})
  MAKE_CPY(1, u64* rp; Arr* r = m_bitarrp(&rp, ia), getU, BIT_PUT, rp, H2T_COPY(Bit),
                                  Bit,          {COPY_FN(1,1),COPY_FN(i8,1),COPY_FN(i16,1),COPY_FN(i32,1),COPY_FN(f64,1),badCopy,      badCopy,       badCopy,       cpyBitArr_B,  COPY_FN(f64,1)})
  
  
  copy_fn tcopy_i8Fns [] = {[t_bitarr]=simd_copy_1u_i8,  [t_i8arr]=simd_copy_i8_i8 ,[t_i8slice]=simd_copy_i8_i8};
  copy_fn tcopy_i16Fns[] = {[t_bitarr]=simd_copy_1u_i16, [t_i8arr]=simd_copy_i8_i16,[t_i8slice]=simd_copy_i8_i16, [t_i16arr]=simd_copy_i16_i16,[t_i16slice]=simd_copy_i16_i16};
  copy_fn tcopy_i32Fns[] = {[t_bitarr]=simd_copy_1u_i32, [t_i8arr]=simd_copy_i8_i32,[t_i8slice]=simd_copy_i8_i32, [t_i16arr]=simd_copy_i16_i32,[t_i16slice]=simd_copy_i16_i32, [t_i32arr]=simd_copy_i32_i32,[t_i32slice]=simd_copy_i32_i32};
  copy_fn tcopy_f64Fns[] = {[t_bitarr]=simd_copy_1u_f64, [t_i8arr]=simd_copy_i8_f64,[t_i8slice]=simd_copy_i8_f64, [t_i16arr]=simd_copy_i16_f64,[t_i16slice]=simd_copy_i16_f64, [t_i32arr]=simd_copy_i32_f64,[t_i32slice]=simd_copy_i32_f64, [t_f64arr]=simd_copy_f64_f64,[t_f64slice]=simd_copy_f64_f64};
  copy_fn tcopy_c8Fns [] = {[t_c8arr]=simd_copy_c8_c8 ,[t_c8slice]=simd_copy_c8_c8};
  copy_fn tcopy_c16Fns[] = {[t_c8arr]=simd_copy_c8_c16,[t_c8slice]=simd_copy_c8_c16, [t_c16arr]=simd_copy_c16_c16,[t_c16slice]=simd_copy_c16_c16};
  copy_fn tcopy_c32Fns[] = {[t_c8arr]=simd_copy_c8_c32,[t_c8slice]=simd_copy_c8_c32, [t_c16arr]=simd_copy_c16_c32,[t_c16slice]=simd_copy_c16_c32, [t_c32arr]=simd_copy_c32_c32,[t_c32slice]=simd_copy_c32_c32};
  
  #define TCOPY_FN(T, N, NUM) static void m_copyG_##N(void* a, usz ms, B x, usz xs, usz l) { \
    if (l==0) return;          \
    void* xp = tyany_ptr(x);   \
    u8 xt = TY(x);             \
    tcopy_##N##Fns[xt]((xs << arrTypeWidthLog(xt)) + (u8*)xp, ms + (T*)a, l, a(x)); \
  }
  TCOPY_FN(i8,i8, 1)
  TCOPY_FN(i16,i16, 1)
  TCOPY_FN(i32,i32, 1)
  TCOPY_FN(u8,c8, 0)
  TCOPY_FN(u16,c16, 0)
  TCOPY_FN(u32,c32, 0)
  TCOPY_FN(f64,f64, 1)
  static void m_copyG_bit(void* a, usz ms, B x, usz xs, usz l) {
    bit_cpyN((u64*)a, ms, bitarr_ptr(x), xs, l);
  }
#else
  #define MAKE_ICPY(T,E) Arr* cpy##T##Arr(B x) { \
    usz ia = IA(x);        \
    E* rp; Arr* r = m_##E##arrp(&rp, ia); \
    arr_shCopy(r, x);      \
    u8 xe = TI(x,elType);  \
    if      (xe==el_bit) { u64* xp = bitarr_ptr(x); for(usz i=0; i<ia; i++) rp[i]=bitp_get(xp,i); } \
    else if (xe==el_i8 ) { i8*  xp = i8any_ptr (x); for(usz i=0; i<ia; i++) rp[i]=xp[i]; } \
    else if (xe==el_i16) { i16* xp = i16any_ptr(x); for(usz i=0; i<ia; i++) rp[i]=xp[i]; } \
    else if (xe==el_i32) { i32* xp = i32any_ptr(x); for(usz i=0; i<ia; i++) rp[i]=xp[i]; } \
    else if (xe==el_f64) { f64* xp = f64any_ptr(x); for(usz i=0; i<ia; i++) rp[i]=xp[i]; } \
    else {                 \
      B* xp = arr_bptr(x); \
      if (xp!=NULL) { for (usz i=0; i<ia; i++) rp[i]=o2fG(xp[i]    ); } \
      else { SGetU(x) for (usz i=0; i<ia; i++) rp[i]=o2fG(GetU(x,i)); } \
    }                      \
    ptr_decT(a(x));        \
    return r;              \
  }

  #define MAKE_CCPY(T,E)   \
  Arr* cpy##T##Arr(B x) {  \
    usz ia = IA(x);        \
    T##Atom* rp; Arr* r = m_##E##arrp(&rp, ia); \
    arr_shCopy(r, x);      \
    u8 xe = TI(x,elType);  \
    if      (xe==el_c8 ) { u8*  xp = c8any_ptr (x); for(usz i=0; i<ia; i++) rp[i]=xp[i]; } \
    else if (xe==el_c16) { u16* xp = c16any_ptr(x); for(usz i=0; i<ia; i++) rp[i]=xp[i]; } \
    else if (xe==el_c32) { u32* xp = c32any_ptr(x); for(usz i=0; i<ia; i++) rp[i]=xp[i]; } \
    else {                 \
      B* xp = arr_bptr(x); \
      if (xp!=NULL) { for (usz i=0; i<ia; i++) rp[i]=o2cG(xp[i]    ); } \
      else { SGetU(x) for (usz i=0; i<ia; i++) rp[i]=o2cG(GetU(x,i)); } \
    }                      \
    ptr_decT(a(x));        \
    return r;              \
  }

  Arr* cpyHArr(B x) {
    usz ia = IA(x);
    HArr_p r = m_harrUc(x);
    u8 xe = TI(x,elType);
    if      (xe==el_bit) { u64* xp = bitarr_ptr(x); for(usz i=0; i<ia; i++) r.a[i]=m_f64(bitp_get(xp, i)); }
    else if (xe==el_i8 ) { i8*  xp = i8any_ptr (x); for(usz i=0; i<ia; i++) r.a[i]=m_f64(xp[i]); }
    else if (xe==el_i16) { i16* xp = i16any_ptr(x); for(usz i=0; i<ia; i++) r.a[i]=m_f64(xp[i]); }
    else if (xe==el_i32) { i32* xp = i32any_ptr(x); for(usz i=0; i<ia; i++) r.a[i]=m_f64(xp[i]); }
    else if (xe==el_f64) { f64* xp = f64any_ptr(x); for(usz i=0; i<ia; i++) r.a[i]=m_f64(xp[i]); }
    else if (xe==el_c8 ) { u8*  xp = c8any_ptr (x); for(usz i=0; i<ia; i++) r.a[i]=m_c32(xp[i]); }
    else if (xe==el_c16) { u16* xp = c16any_ptr(x); for(usz i=0; i<ia; i++) r.a[i]=m_c32(xp[i]); }
    else if (xe==el_c32) { u32* xp = c32any_ptr(x); for(usz i=0; i<ia; i++) r.a[i]=m_c32(xp[i]); }
    else {
      B* xp = arr_bptr(x);
      if (xp!=NULL) { for (usz i=0; i<ia; i++) r.a[i] = inc(xp[i]); }
      else { SGet(x)  for (usz i=0; i<ia; i++) r.a[i] = Get(x, i);  }
    }
    NOGC_E;
    decG(x);
    return (Arr*)r.c;
  }
  Arr* cpyBitArr(B x) {
    usz ia = IA(x);
    u64* rp; Arr* r = m_bitarrp(&rp, ia);
    arr_shCopy(r, x);
    u8 xe = TI(x,elType);
    if      (xe==el_bit) { u64* xp = bitarr_ptr(x); for(usz i=0; i<BIT_N(ia); i++) rp[i] = xp[i]; }
    else if (xe==el_i8 ) { i8*  xp = i8any_ptr (x); for(usz i=0; i<ia; i++) bitp_set(rp,i,xp[i]); }
    else if (xe==el_i16) { i16* xp = i16any_ptr(x); for(usz i=0; i<ia; i++) bitp_set(rp,i,xp[i]); }
    else if (xe==el_i32) { i32* xp = i32any_ptr(x); for(usz i=0; i<ia; i++) bitp_set(rp,i,xp[i]); }
    else if (xe==el_f64) { f64* xp = f64any_ptr(x); for(usz i=0; i<ia; i++) bitp_set(rp,i,xp[i]); }
    else {
      B* xp = arr_bptr(x);
      if (xp!=NULL) { for (usz i=0; i<ia; i++) bitp_set(rp,i,o2fG(xp[i]    )); }
      else { SGetU(x) for (usz i=0; i<ia; i++) bitp_set(rp,i,o2fG(GetU(x,i))); }
    }
    ptr_decT(a(x));
    return r;
  }

#endif

MAKE_ICPY(I8, i8)
MAKE_ICPY(I16, i16)
MAKE_ICPY(I32, i32)
MAKE_ICPY(F64, f64)

MAKE_CCPY(C8, c8)
MAKE_CCPY(C16, c16)
MAKE_CCPY(C32, c32)
#undef BIT_PUT
#undef MAKE_CCPY
#undef MAKE_ICPY
#undef MAKE_CPY
#undef COPY_FN




static B m_getU_MAX(void* a, usz ms) { err("m_setG_MAX"); }
static B m_getU_bit(void* a, usz ms) { return m_i32(bitp_get(((u64*) a), ms)); }
static B m_getU_i8 (void* a, usz ms) { return m_i32(((i8* ) a)[ms]); }
static B m_getU_i16(void* a, usz ms) { return m_i32(((i16*) a)[ms]); }
static B m_getU_i32(void* a, usz ms) { return m_i32(((i32*) a)[ms]); }
static B m_getU_c8 (void* a, usz ms) { return m_c32(((u8* ) a)[ms]); }
static B m_getU_c16(void* a, usz ms) { return m_c32(((u16*) a)[ms]); }
static B m_getU_c32(void* a, usz ms) { return m_c32(((u32*) a)[ms]); }
static B m_getU_f64(void* a, usz ms) { return m_f64(((f64*) a)[ms]); }
static B m_getU_B  (void* a, usz ms) { return         ((B*) a)[ms]; }

M_CopyF copyFns[el_MAX];
M_FillF fillFns[el_MAX];

MutFns mutFns[el_MAX+1];
u8 el_orArr[el_MAX*16 + el_MAX+1];
void mutF_init(void) {
  for (u8 i = 0; i <= el_MAX; i++) {
    for (u8 j = 0; j <= el_MAX; j++) {
      u8 el;
      if (i==el_MAX|j==el_MAX) el = i>j?j:i;
      else if (elNum(i) && elNum(j)) el = i>j?i:j;
      else if (elChr(i) && elChr(j)) el = i>j?i:j;
      else el = el_B;
      el_orArr[i*16 + j] = el;
    }
  }
  #define FOR_TY(A) A(MAX) A(bit) A(i8) A(i16) A(i32) A(c8) A(c16) A(c32) A(f64) A(B)
  #define F(T,N) mutFns[el_##T].m_##N = m_##N##_##T;
  #define G(T) F(T,set) F(T,setG) F(T,getU) F(T,fill) F(T,fillG) F(T,copy) F(T,copyG)
  FOR_TY(G)
  #undef G
  #undef F
  mutFns[el_bit].elType = el_bit; mutFns[el_bit].valType = t_bitarr;
  mutFns[el_i8 ].elType = el_i8 ; mutFns[el_i8 ].valType = t_i8arr;
  mutFns[el_i16].elType = el_i16; mutFns[el_i16].valType = t_i16arr;
  mutFns[el_i32].elType = el_i32; mutFns[el_i32].valType = t_i32arr;
  mutFns[el_c8 ].elType = el_c8 ; mutFns[el_c8 ].valType = t_c8arr;
  mutFns[el_c16].elType = el_c16; mutFns[el_c16].valType = t_c16arr;
  mutFns[el_c32].elType = el_c32; mutFns[el_c32].valType = t_c32arr;
  mutFns[el_f64].elType = el_f64; mutFns[el_f64].valType = t_f64arr;
  mutFns[el_B  ].elType = el_B  ; mutFns[el_B  ].valType = t_harr;
  mutFns[el_MAX].elType = el_MAX; mutFns[el_MAX].valType = t_COUNT;
  for (u8 i = 0; i < el_MAX; i++) copyFns[i] = mutFns[i].m_copyG;
  for (u8 i = 0; i < el_MAX; i++) fillFns[i] = mutFns[i].m_fillG;
}
