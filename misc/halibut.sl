% Halibut mode for Jed.

$1 = "Halibut";
create_syntax_table ($1);

define_syntax ("\#", "", '%', $1);       % Comment Syntax
define_syntax ('\\', '\\', $1);         % Quote character
define_syntax ("{", "}", '(', $1);    %  are all these needed?
define_syntax ("a-zA-Z0-9", 'w', $1);
set_syntax_flags ($1, 8);

#ifdef HAS_DFA_SYNTAX
%enable_highlight_cache ("halibut.dfa", $1);

% A braced comment in Halibut is \#{ ... }, where ... may contain
% any correctly nested sequence of braces. Of course we can't match
% that in a DFA rule, so we'll go down to a reasonable depth of 3
% instead.
#ifexists dfa_define_highlight_rule
dfa_define_highlight_rule ("\\\\#{[^{}]*({[^{}]*({[^}]*}[^{}]*)*}[^{}]*)*}",
			   "Qcomment", $1);

dfa_define_highlight_rule ("\\\\#.*$", "comment", $1);
dfa_define_highlight_rule ("^\\\\c([ \t].*)?$", "string", $1);
dfa_define_highlight_rule ("\\\\[\\\\{}\\-_]", "keyword0", $1);
dfa_define_highlight_rule ("\\\\[A-Za-tv-z][A-Za-z0-9]*", "keyword0", $1);
dfa_define_highlight_rule ("\\\\u[A-Fa-f0-9][A-Fa-f0-9][A-Fa-f0-9][A-Fa-f0-9]",
			   "keyword0", $1);
dfa_define_highlight_rule ("\\\\u[A-Fa-f0-9]?[A-Fa-f0-9]?[A-Fa-f0-9]?[A-Fa-f0-9]",
			   "keyword1", $1);
dfa_define_highlight_rule ("[{}]", "delimiter", $1);
dfa_define_highlight_rule (".", "normal", $1);
dfa_build_highlight_table ($1);
#else
define_highlight_rule ("\\\\#{[^{}]*({[^{}]*({[^}]*}[^{}]*)*}[^{}]*)*}",
		       "Qcomment", $1);

define_highlight_rule ("\\\\#.*$", "comment", $1);
define_highlight_rule ("^\\\\c([ \t].*)?$", "string", $1);
define_highlight_rule ("\\\\[\\\\{}\\-_]", "keyword0", $1);
define_highlight_rule ("\\\\[A-Za-tv-z][A-Za-z0-9]*", "keyword0", $1);
define_highlight_rule ("\\\\u[A-Fa-f0-9][A-Fa-f0-9][A-Fa-f0-9][A-Fa-f0-9]",
		       "keyword0", $1);
define_highlight_rule ("\\\\u[A-Fa-f0-9]?[A-Fa-f0-9]?[A-Fa-f0-9]?[A-Fa-f0-9]",
		       "keyword1", $1);
define_highlight_rule ("[{}]", "delimiter", $1);
define_highlight_rule (".", "normal", $1);
build_highlight_table ($1);
#endif
#endif

%  This hook identifies lines containing comments as paragraph separator
define halibut_is_comment() {
    bol ();
    while (ffind ("\\\\#")) go_right (3);
    ffind ("\\#"); % return value on stack
}

variable Halibut_Ignore_Comment = 0;  % if true, line containing a comment
                                       % does not delimit a paragraph

define halibut_paragraph_separator() {
    bol();
    skip_white();
    if (eolp())
	return 1;
    if (looking_at("\\c ") or looking_at("\\c\t") or
	looking_at("\\c\n"))
	return 1;
    return not (Halibut_Ignore_Comment) and halibut_is_comment();
} 

define halibut_wrap_hook() {
    variable yep;
    push_spot ();
    yep = up_1 () and halibut_is_comment ();
    pop_spot ();
    if (yep) {
	push_spot ();
	bol_skip_white ();
	insert ("\\# ");
	pop_spot ();
    }
}

#ifexists mode_set_mode_info
mode_set_mode_info("Halibut", "fold_info", "\\# {{{\r\\# }}}\r\r");
#endif

define halibut_mode() {
    variable mode = "Halibut";
    % use_keymap (mode);
    set_mode (mode, 0x1 | 0x20);
    set_buffer_hook ("par_sep", "halibut_paragraph_separator");
    set_buffer_hook ("wrap_hook", "halibut_wrap_hook");
    use_syntax_table (mode);
    runhooks ("halibut_mode_hook");
}
