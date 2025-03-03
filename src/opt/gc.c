#include "gc.h"

#if ENABLE_GC
  static void mm_freeFreedAndMerge(void);
#endif

#ifdef LOG_GC
  #include "../utils/time.h"
#endif

u64 gc_depth = 1;


vfn gc_roots[8];
u32 gc_rootSz;
void gc_addFn(vfn f) {
  if (gc_rootSz>=8) err("Too many GC root functions");
  gc_roots[gc_rootSz++] = f;
}

Value* gc_rootObjs[512];
u32 gc_rootObjSz;
void gc_add(B x) {
  assert(isVal(x));
  if (gc_rootObjSz>=512) err("Too many GC root objects");
  gc_rootObjs[gc_rootObjSz++] = v(x);
}

B* gc_rootBRefs[64]; u32 gc_rootBRefsSz;
void gc_add_ref(B* x) {
  if (gc_rootBRefsSz>=64) err("Too many GC root B refs");
  gc_rootBRefs[gc_rootBRefsSz++] = x;
}





static void gc_resetTag(Value* x) {
  x->mmInfo&= 0x7F;
}

void gc_visitRoots() {
  for (u32 i = 0; i < gc_rootSz; i++) gc_roots[i]();
  for (u32 i = 0; i < gc_rootObjSz; i++) mm_visitP(gc_rootObjs[i]);
  for (u32 i = 0; i < gc_rootBRefsSz; i++) mm_visit(*gc_rootBRefs[i]);
}

static void gc_tryFree(Value* v) {
  u8 t = v->type;
  #if defined(DEBUG) && !defined(CATCH_ERRORS)
    if (t==t_freed) err("GC found t_freed\n");
  #endif
  if (t!=t_empty && !(v->mmInfo&0x80)) {
    if (t==t_shape || t==t_temp || t==t_talloc) return;
    #ifdef DONT_FREE
      v->flags = t;
    #else
      #if CATCH_ERRORS
        if (t==t_freed) { mm_free(v); return; }
      #endif
    #endif
    #ifdef LOG_GC
      gc_freedBytes+= mm_size(v); gc_freedCount++;
    #endif
    v->type = t_freed;
    ptr_inc(v); // required as otherwise the object may free itself while not done reading its own fields
    TIi(t,freeO)(v);
    tptr_dec(v, mm_free);
    // Object may not be immediately freed if it's not a part of a cycle, but instead a descendant of one.
    // It will be freed when the cycle is freed, and the t_freed type ensures it doesn't double-free itself
  }
}

#ifdef LOG_GC
  u64 gc_visitBytes, gc_visitCount, gc_freedBytes, gc_freedCount, gc_unkRefsBytes, gc_unkRefsCount;
#endif

i32 visit_mode;
enum {
  GC_DEC_REFC, // decrement refcount
  GC_INC_REFC, // increment refcount
  GC_MARK,     // if unmarked, mark & visit
};

void gc_onVisit(Value* x) {
  switch (visit_mode) { default: UD;
    case GC_DEC_REFC:
      #if DEBUG
        if(x->refc==0) err("decrementing refc 0");
      #endif
      x->refc--;
      return;
    case GC_INC_REFC: x->refc++; return;
    case GC_MARK: {
      if (x->mmInfo&0x80) return;
      x->mmInfo|= 0x80;
      #ifdef LOG_GC
        gc_visitBytes+= mm_size(x); gc_visitCount++;
      #endif
      TIv(x,visit)(x);
      return;
    }
  }
}

static void gcv2_visit(Value* x) {
  TIv(x,visit)(x);
}
static void gcv2_unmark_visit(Value* x) {
  gc_resetTag(x);
  TIv(x,visit)(x);
}

#if HEAP_VERIFY
  void gcv2_runHeapverify(i32 mode) {
    visit_mode = mode==0? GC_DEC_REFC : GC_INC_REFC;
    mm_forHeap(gcv2_visit);
    gc_visitRoots();
  }
#endif

#if ENABLE_GC
  static void gc_visitRefcNonzero(Value* x) {
    if (x->refc == 0) return;
    #ifdef LOG_GC
      gc_unkRefsBytes+= mm_size(x); gc_unkRefsCount++;
    #endif
    mm_visitP(x);
  }
  
  static void gc_run(bool toplevel) {
    if (toplevel) {
      mm_forHeap(gc_resetTag);
    } else {
      visit_mode = GC_DEC_REFC;
      mm_forHeap(gcv2_unmark_visit);
      
      visit_mode = GC_MARK;
      mm_forHeap(gc_visitRefcNonzero);
      
      visit_mode = GC_INC_REFC;
      mm_forHeap(gcv2_visit);
    }
    
    visit_mode = GC_MARK;
    gc_visitRoots();
    
    mm_forHeap(gc_tryFree);
    mm_freeFreedAndMerge();
  }
#endif

u64 gc_lastAlloc;
void gc_forceGC(bool toplevel) {
  #if ENABLE_GC
    #ifdef LOG_GC
      u64 start = nsTime();
      gc_visitBytes = 0; gc_freedBytes = 0;
      gc_visitCount = 0; gc_freedCount = 0;
      gc_unkRefsBytes = 0; gc_unkRefsCount = 0;
      u64 startSize = mm_heapUsed();
    #endif
      gc_run(toplevel);
    u64 endSize = mm_heapUsed();
    #ifdef LOG_GC
      fprintf(stderr, "GC kept "N64d"B/"N64d" objs, freed "N64d"B, incl. directly "N64d"B/"N64d" objs", gc_visitBytes, gc_visitCount, startSize-endSize, gc_freedBytes, gc_freedCount);
      fprintf(stderr, "; unknown refs: "N64d"B/"N64d" objs", gc_unkRefsBytes, gc_unkRefsCount);
      fprintf(stderr, "; took %.3fms\n", (nsTime()-start)/1e6);
    #endif
    gc_lastAlloc = endSize;
  #endif
}

static bool gc_wantTopLevelGC;
bool gc_maybeGC(bool toplevel) {
  if (gc_depth) return false;
  u64 used = mm_heapUsed();
  if (used > gc_lastAlloc*2 || (toplevel && gc_wantTopLevelGC)) {
    if (toplevel) gc_wantTopLevelGC = false;
    gc_forceGC(toplevel);
    return true;
  }
  return false;
}
