/* -*- c -*-
 * Name: halibut
 * Description: Halibut document formatter.
 * Author: Simon Tatham <anakin@pobox.com>
 */

state halibut_paragraph extends Highlight
{
    /^[[:space:]]*$/ {
	language_print($0);
	return;
    }
}

state halibut_nested_braces extends Highlight
{
    BEGIN {
	nestlevel = 1;
    }
    
    /{/ {
	language_print($0);
	nestlevel++;
    }

    /}/ {
	language_print($0);
	nestlevel--;
	if (nestlevel == 0)
	    return;
    }
}

state halibut extends HighlightEntry
{
    /* one-non-letter commands */
    /\\\\[-\\\\_{}.]/ {
	keyword_face(true);
	language_print($0);
	keyword_face(false);
    }

    /* code paragraphs */
    /^\\\\c / {
	keyword_face(true);
	language_print($0);
	keyword_face(false);
	string_face(true);
	call(eat_one_line);
	string_face(false);
    }

    /* emphasis in code paragraphs */
    /^\\\\e / {
	keyword_face(true);
	language_print($0);
	keyword_face(false);
	builtin_face(true);
	call(eat_one_line);
	builtin_face(false);
    }

    /* \uXXXX Unicode commands */
    /\\\\u[0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F]/ {
	keyword_face(true);
	language_print($0);
	keyword_face(false);
    }

    /* multi letter commands */
    /\\\\[0-9a-tv-zA-Z][0-9a-zA-Z]*/ {
	keyword_face(true);
	language_print($0);
	keyword_face(false);
    }

    /* paragraph-type comments */
    /\\\\#/ {
	comment_face(true);
	language_print($0);
	call(halibut_paragraph);
	comment_face(false);
    }

    /* intra-paragraph type comments */
    /\\\\#{/ {
	comment_face(true);
	language_print($0);
	call(halibut_nested_braces);
	comment_face(false);
    }

    /* I want to have braces highlighted; they're *special* */
    /[{}]/ {
	keyword_face(true);
	language_print($0);
	keyword_face(false);
    }
}
