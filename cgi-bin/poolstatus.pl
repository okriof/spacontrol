#!/usr/bin/perl
print "Content-type: text/html\n\n";

open(FH, "</home/pi/poolfiles/poolState");
$statString = <FH>;
close(FH);

print "$statString";