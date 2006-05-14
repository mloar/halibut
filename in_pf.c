#include <stdio.h>
#include "halibut.h"
#include "paper.h"

void read_pfa_file(input *in) {
    char buf[512], *p;
    size_t len;
    char *fontname;
    font_info *fi;

    len = fread(buf, 1, sizeof(buf) - 1, in->currfp);
    buf[len] = 0;
    if (strncmp(buf, "%!FontType1-", 12) &&
	strncmp(buf, "%!PS-AdobeFont-", 15))
	return;
    p = buf;
    p += strcspn(p, ":") + 1;
    p += strspn(p, " \t");
    len = strcspn(p, " \t");
    fontname = snewn(len + 1, char);
    memcpy(fontname, p, len);
    fontname[len] = 0;
    for (fi = all_fonts; fi; fi = fi->next) {
	if (strcmp(fi->name, fontname) == 0) {
	    fi->fp = in->currfp;
	    sfree(fontname);
	    return;
	}
    }
    fclose(in->currfp);
    sfree(fontname);
}
	
	
