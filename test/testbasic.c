/*
 * testbasic.c: checks that the library builds, links against libxml2
 * and reports a consistent version
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <stdio.h>
#include <string.h>

#include <libxml/parser.h>
#include <libxml/tree.h>

#include <libxpath3/xpath3.h>

static int failures = 0;

static void
check(int cond, const char *what) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

int
main(void) {
    static const char xml[] = "<invoice><line>1</line></invoice>";
    xmlDocPtr doc;

    check(strcmp(xp3GetVersion(), XP3_VERSION_STRING) == 0,
          "run-time version string matches headers");
    check(xp3GetVersionNumber() == XP3_VERSION_NUMBER,
          "run-time version number matches headers");

    doc = xmlReadMemory(xml, (int) sizeof(xml) - 1, "test.xml", NULL, 0);
    check(doc != NULL, "libxml2 parses a document");
    if (doc != NULL) {
        xmlNodePtr root = xmlDocGetRootElement(doc);

        check(root != NULL &&
              xmlStrEqual(root->name, BAD_CAST "invoice"),
              "root element is <invoice>");
        xmlFreeDoc(doc);
    }

    if (failures == 0)
        printf("libxpath3 %s, libxml2 %s: OK\n",
               xp3GetVersion(), xmlParserVersion);
    return(failures == 0 ? 0 : 1);
}
