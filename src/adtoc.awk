#!/bin/awk -f
# $Id: adtoc.awk,v 1.1 2022/11/15 16:51:51 alx Exp $

BEGIN {
	print "/* Generated from " ARGV[1] " */"
	name = ARGV[1];
	sub("\\.", "_", name);
	print "const char *" name "[] = {";
}

!/^[ \t]*!/ && !/^[ \t\n]*$/ {
	print "\t\"" $0 "\",";
}

END {
	print "\t0l\n};";
}
