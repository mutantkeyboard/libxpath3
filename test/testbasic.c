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

#include "testtap.h"

static void
check(int cond, const char *what) {
    xp3TestCheck(cond, what);
}

static void
testVersion(void) {
    check(strcmp(xp3GetVersion(), XP3_VERSION_STRING) == 0,
          "run-time version string matches headers");
    check(xp3GetVersionNumber() == XP3_VERSION_NUMBER,
          "run-time version number matches headers");
}

static void
testLibxml2(void) {
    static const char xml[] = "<root><item>1</item></root>";
    xmlDocPtr doc;

    doc = xmlReadMemory(xml, (int) sizeof(xml) - 1, "test.xml", NULL, 0);
    check(doc != NULL, "libxml2 parses a document");
    if (doc != NULL) {
        xmlNodePtr root = xmlDocGetRootElement(doc);

        check(root != NULL &&
              xmlStrEqual(root->name, BAD_CAST "root"),
              "root element is <root>");
        xmlFreeDoc(doc);
    }
}

int
main(void) {
    printf("# libxpath3 %s, libxml2 %s\n", xp3GetVersion(),
           xmlParserVersion);

    XP3_TEST_RUN(testVersion);
    XP3_TEST_RUN(testLibxml2);

    return(xp3TestDone());
}
