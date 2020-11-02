#!/usr/bin/perl
print "Content-type: text/html\n\n";

$qs = substr($ENV{'QUERY_STRING'}, 0, 1);

open(FH, ">/home/pi/poolfiles/poolCmd");
print FH "$qs";
close(FH);

print "OK\n";
