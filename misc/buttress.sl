% The functions here are common to both TeX and LaTeX modes.

$1 = "Buttress";
create_syntax_table ($1);

define_syntax ("\#", "", '%', $1);       % Comment Syntax
define_syntax ('\\', '\\', $1);         % Quote character
define_syntax ("{", "}", '(', $1);    %  are all these needed?
define_syntax ("a-zA-Z0-9", 'w', $1);
set_syntax_flags ($1, 8);

#ifdef HAS_DFA_SYNTAX
%enable_highlight_cache ("buttress.dfa", $1);
define_highlight_rule ("\\\\#.*$", "comment", $1);
define_highlight_rule ("^\\\\c([ \t].*)?$", "string", $1);
define_highlight_rule ("\\\\[\\\\{}]", "keyword0", $1);
define_highlight_rule ("\\\\[A-Za-tv-z][A-Za-z]*", "keyword0", $1);
define_highlight_rule ("\\\\u[A-Fa-f0-9][A-Fa-f0-9][A-Fa-f0-9][A-Fa-f0-9]",
		       "keyword0", $1);
define_highlight_rule ("\\\\u[A-Fa-f0-9]?[A-Fa-f0-9]?[A-Fa-f0-9]?[A-Fa-f0-9]",
		       "keyword1", $1);
define_highlight_rule ("[{}]", "delimiter", $1);
define_highlight_rule (".", "normal", $1);
build_highlight_table ($1);
#endif

%  This hook identifies lines containing TeX comments as paragraph separator
define buttress_is_comment() {
    bol ();
    while (ffind ("\\\\#")) go_right (3);
    ffind ("\\#"); % return value on stack
}

variable Buttress_Ignore_Comment = 0;  % if true, line containing a comment
                                       % does not delimit a paragraph

define buttress_paragraph_separator() {
    bol();
    skip_white();
    if (eolp())
	return 1;
    if (looking_at("\\c ") or looking_at("\\c\t") or
	looking_at("\\c\n"))
	return 1;
    return not (Buttress_Ignore_Comment) and buttress_is_comment();
} 

define buttress_wrap_hook() {
    variable yep;
    push_spot ();
    yep = up_1 () and buttress_is_comment ();
    pop_spot ();
    if (yep) {
	push_spot ();
	bol_skip_white ();
	insert ("\\# ");
	pop_spot ();
    }
}

define buttress_mode() {
    variable mode = "Buttress";
    % use_keymap (mode);
    set_mode (mode, 0x1 | 0x20);
    set_buffer_hook ("par_sep", "buttress_paragraph_separator");
    set_buffer_hook ("wrap_hook", "buttress_wrap_hook");
    use_syntax_table (mode);
    runhooks ("buttress_mode_hook");
}
