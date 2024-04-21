#!/bin/awk -f

# Copyright (C) 2022-2024 alx@fastestcode.org
# This software is distributed under the terms of the X/MIT license.
# See the included COPYING file for further information.

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
