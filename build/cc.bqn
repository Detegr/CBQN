#!/usr/bin/env bqn
# Modified version of https://github.com/mlochbaum/BQN/blob/master/src/cjs.bqn, which is under the ISC license (https://github.com/mlochbaum/BQN/blob/master/LICENSE)

args←•args
"Usage: ./cc.bqn path/to/mlochbaum/BQN [-i] <(r|r0|r1|c|cc|f|e|p) or an expression>"!2≤≠args
return ← 1≡⊑args
args↓˜↩ return
path←(⊑args)∾"/src/"
args↓˜↩1
Import ← {𝕨•Import path∾𝕩}
FChars ← {𝕨•FChars path∾𝕩}
FLines ← {𝕨•FLines path∾𝕩}


F ← •Repr  # Format number
# Escape the special characters that appear in BQN sources.
Esc←{
  in ← (@+0‿9‿10‿13)∾"'"""   # Null, Tab, LF, CR, and quotes
  out ← "0tnr"               # Whitespace characters changed to letters
  i ← in⊐𝕩
  𝕩 ↩ i ⊏⟜out⌾((i<≠out)⊸/) 𝕩 # Replace
  ∾(i<≠in) /⟜"\"⊸∾¨ 𝕩        # Insert \
}

U32Len ← (≠ - ·+´ ≥⟜55296 ∧ ≤⟜56319)-⟜@ # for dzaima/BQN; shouldn't break things in proper BQN impls, as surrogate values don't map to valid codepoints
OStr ← { # A BQN string
  a←∧´𝕩<128+@
  ∾⟨a⊑"m_c32vec"‿"m_c8vec","(",a¬⊸⥊"U","""",Esc𝕩,""",",•Repr U32Len 𝕩,")"⟩
}
OChr ← {"m_c32(U'"∾(Esc⥊𝕩)∾"')"} # A BQN character
ONum ← {s←"-"/˜𝕩<0 ⋄ ∞⊸=∘|◶⟨"m_f64("∾")"∾˜s∾F∘| ⋄ "m_f64("∾s∾"1.0/0.0)"⟩𝕩} # Format number
OAny ← ≡◶⟨@⊸≤◶ONum‿OChr, OStr, ⊑⟩

List ← (0<≠)◶⟨"",1↓·∾","⊸∾¨⟩
ShortList ← {"m_lv"∾𝕨∾"_"∾(F≠𝕩)∾"("∾(List𝕩)∾")"}
LongList ← {"m_ca"∾𝕨∾"("∾(F≠𝕩)∾",("∾𝕨∾"[]){"∾(List𝕩)∾"})"}
ConstList ← {"m_ca"∾𝕨∾"("∾(F≠𝕩)∾",({static "∾𝕨∾" l_[]={"∾(List𝕩)∾"};l_;}))"}
_makeList ← {𝕨 (≠∘⊢>4˙)◶⟨ShortList,𝕗⊑LongList‿ConstList⟩ 𝕩}
Li ←  {"i32" 1 _makeList F¨𝕩}
_Lo ← {"B"   0 _makeList 𝔽¨𝕩}


glyphs ← Import "glyphs.bqn"
_getComp ← { (4+2×useInd)↑ <∘⊢∾˜ 5↑ (𝕗 Import "c.bqn"){𝔽} }
useInd ← "-i"≡⊑args ⋄ args↓˜↩useInd
Comp ← ((<"incG(runtime["∾F∾"])"˙)¨↕≠∾glyphs) glyphs _getComp ⊢
J ← ∾∾⟜(@+10)¨

Fout ← {((≠𝕩)↑⟨Li ⋄ OAny _Lo ⋄ =◶⟨ONum,Li _Lo⟩ _Lo _Lo ⋄ (Li 2⊸↑) _Lo ⋄ Li _Lo ⋄ OStr⟩) {𝕎𝕩}¨ 𝕩}
Frun ← 1⊸Fout
Long ← {¯2↓∾𝕩∾¨<","∾@+10}
LFC ← Long∘Fout∘Comp

RT ← {
  src‿need‿inputs←𝕩Import"pr.bqn"
  pr←"runtime_0"‿"provide"{(∾𝕨<⊸(<∘{∾"incG("‿𝕨‿"["‿𝕩‿"])"}⟜F¨)⟜(↕≠)¨𝕩)⊏˜(∾𝕩)⊐∾need}○((-1+1=𝕩)⊸↑)inputs
  Long Fout pr need _getComp src
}
CArg ← {J (¯5⊸↓∾𝕩˙)⌾⊑ FLines "c.bqn"}
SVG ← {∾⟨"Modify←GetHighlights←⊢⋄"⟩∾ FChars∘∾⟜".bqn"¨ "../svg"‿𝕩}

(@+10)∾˜ •Out⍟(¬return) (⊑"r"‿"r0"‿"r1"‿"c"‿"cc"‿"f"‿"e"‿"eu"‿"p"⊐⊏)◶⟨
  RT∘2, RT∘0, RT∘1
  {𝕩⋄LFC CArg "⟨"∾"⟩"«∾","⊸∾¨'"'(⊣∾∾˜)¨glyphs}
  {𝕩⋄LFC "{"∾"}"∾˜CArg"𝕩"}
  {𝕩⋄LFC FChars "f.bqn"}
  {𝕩⋄LFC SVG "e"}
  {𝕩⋄LFC FChars "eu.bqn"}
  {𝕩⋄LFC SVG "p"}
  ∾LFC¨
  # ¯1 ↓ · J L∘Fout∘Comp¨
⟩ args