" Vim syntax file
" Language:	Halibut
" Maintainer:	Jacob Nevins <jacobn+vim@chiark.greenend.org.uk>
" URL:          http://www.chiark.greenend.org.uk/~sgtatham/halibut/
" Filenames:    *.but
" Version:      $Id: halibut.vim,v 1.9 2004/04/02 00:03:10 jtn Exp $

" I've never been entirely comfortable with vim's syntax highlighting
" facilities, so this may have all sorts of nasty loose ends, corner cases
" etc, but it works for me.
" I have no idea if it's compatible with vim <6.1.

" Based on docs in Halibut CVS 2004-03-31

" FIXME:
"   - sync - last blank line, \quote, \lcont
"   - add "display" etc for speed?

" Rune from vim 6.1 help
" For version 5.x: Clear all syntax items
" For version 6.x: Quit when a syntax file was already loaded
if version < 600
  syn clear
elseif exists("b:current_syntax")
  finish
endif

" Halibut is case-sensitive.
syn case match

" Fallbacks -- if these characters are seen in text and not caught by
" anything below, it's probably a syntax violation.
syn match butIllegalChar "{"
syn match butIllegalChar "}"
syn match butIllegalChar "\\"

" Simple-minded fallback to highlight any command we don't recognise,
" and assume it has textual arguments.
" (matches current iscmd() in input.c; there are some oddballs which
" don't fit this handled specially below)
syn match butCmdGeneric "\\[A-Za-z0-9]\+" nextgroup=butTextArg

syn cluster butText contains=butLiteral,@butCmd,butTodo

" The special one-character "commands".
" XXX might want to split up? Can all these appear in \k{...}?
syn match butLiteral "\\[-{}_.\\]"

" This isn't specific to Halibut, but is often useful.
syn keyword butTodo XXX FIXME TODO

" Specific processing comes after the generic stuff above.

" Paragraph level stuff.

" Literals -- might need to come before \e{}, \c{}
syn region butQuoteLit matchgroup=butCmdSpecific start="\\c\_s\@=" matchgroup=NONE end="$"
syn region butQLEmph   matchgroup=butCmdSpecific start="\\e\_s\@=" matchgroup=NONE end="$" contains=butQLEmphInv
" Highlight invalid characters in emphasis line, cos I'll never remember.
syn match butQLEmphInv "\S\@=[^bi]" contained

" Paragraph level comment -- might need to come before inline comment.
syn region butCommentPara start="\\#\_s\@=" end="^\s*$" contains=butTodo

" Inline comments -- nested braces are honoured.
syn region butComment matchgroup=Comment start="\\#{" skip="\\}" end="}" contains=butCommentBrace,butTodo
syn region butCommentBrace start="{" skip="\\}" end="}" contains=butCommentBrace,butTodo contained transparent

" Section headings - a bit hairy. Order wrt rest of file is important.
syn match butCmdSpecific "\\\(S\d\|[CAHS]\)" nextgroup=butIdentArgH
" butIdentArgH -> butTextArgH? -> this, which highlights the rest of the para:
syn region butTextHeading start="" end="^\s*$" contained contains=@butText
" Unadorned headings
syn match butCmdSpecific "\\U\_s\@=" nextgroup=butTextHeading
" ...and overall title
syn match butCmdSpecific "\\title\_s\@=" nextgroup=butTextHeading

" Bulleted lists -- arguments optional
syn match butCmdSpecific "\\\(b\|n\|dd\)[^A-Za-z0-9]\@=" nextgroup=butIdentArg

" Config
syn match butCmdSpecific "\\cfg{\@=" nextgroup=butCfgArg

" Index/biblio stuff
syn match butCmdSpecific "\\IM{\@=" nextgroup=butIMArg
syn match butCmdSpecific "\\BR\={\@=" nextgroup=butIdentArg
syn match butCmdSpecific "\\nocite{\@=" nextgroup=butIdentArg

" Macros
syn match butCmdSpecific "\\define{\@=" nextgroup=butIdentArg

" Specific inline commands
" (Some of these are defined out of paranoia about clashes with code quotes.)
" Formatting.
syn match butCmdSpecific "\\e{\@=" nextgroup=butEmphArg
syn match butCmdSpecific "\\c{\@=" nextgroup=butTextArg
syn match butCmdSpecific "\\cw{\@=" nextgroup=butTextArg
syn match butCmdSpecific "\\W{\@=" nextgroup=butHyperArg
" Indexing -- invisible entries.
syn match butCmdSpecific "\\I{\@=" nextgroup=butIndexArg
" Xref commands
syn match butCmdSpecific "\\[kK]{\@=" nextgroup=butIdentArg
" Unicode literal -- a bit special.
syn match butLiteral     "\\u\x\{,4}" nextgroup=butTextArg

" Command cluster.
syn cluster butCmd contains=butCmdGeneric,butCmdSpecific,butComment,butQuoteLit,butQLEmph,butCommentPara,butLiteral

" Types of argument. XXX is this cluster still useful?
syn cluster butArgument contains=butTextArg,butIdentArg,butEmphArgmbutCfgArg,butIdentArgH,butTextArgH
" Generic identifier.
syn region butIdentArg matchgroup=butDelimiter start="{" skip="\\}" end="}" nextgroup=@butArgument contained contains=butLiteral
" Specific chain for headings (needs to come after other heading material)
syn region butTextArgH matchgroup=butDelimiter start="{" skip="\\}" end="}" nextgroup=butTextHeading contained contains=@butText
syn region butIdentArgH matchgroup=butDelimiter start="{" skip="\\}" end="}" nextgroup=butTextArgH,butTextHeading contained contains=butLiteral
" Specific hack for \cfg{}
syn region butCfgArg   matchgroup=butDelimiter start="{" skip="\\}" end="}" nextgroup=butCfgArg contained contains=butLiteral
" Generic argument to be emphasised
syn region butEmphArg  matchgroup=butDelimiter start="{" skip="\\}" end="}" contained contains=@butText
" Specific hack for \W{}{}
syn region butHyperArg matchgroup=butDelimiter start="{" skip="\\}" end="}" contained nextgroup=butTextArg
" Specific hack for \I{}
syn region butIndexArg matchgroup=butDelimiter start="{" skip="\\}" end="}" contained contains=@butText
" Specific hack for \IM{}{}...
syn region butIMArg    matchgroup=butDelimiter start="{" skip="\\}" end="}" contained nextgroup=butIMArg contains=@butText
" Default argument (due to being last).
syn region butTextArg  matchgroup=butDelimiter start="{" skip="\\}" end="}" nextgroup=@butArgument contained contains=@butText transparent

" Rune from vim 6.1 help
if version >= 508 || !exists("did_halibut_syn_inits")
  if version < 508
    let did_halibut_syn_inits = 1
    command -nargs=+ HiLink hi link <args>
  else
    command -nargs=+ HiLink hi def link <args>
  endif

  HiLink butCmdGeneric  Statement
  HiLink butCmdSpecific Statement

  HiLink butLiteral     SpecialChar

  HiLink butQLEmphInv   Error
  HiLink butIllegalChar Error

  HiLink butComment     Comment
  HiLink butCommentPara Comment

  HiLink butDelimiter   Delimiter

  HiLink butIdentArg    Identifier
  HiLink butIdentArgH   Identifier
  HiLink butCfgArg      Identifier
  HiLink butEmphArg     Underlined
  HiLink butHyperArg    Underlined
  HiLink butIndexArg    Identifier
  HiLink butIMArg       Identifier

  HiLink butTextHeading Underlined

  HiLink butTodo        Todo

  delcommand HiLink
endif

" b: local to current buffer
let b:current_syntax = "halibut"
