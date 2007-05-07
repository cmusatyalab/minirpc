#!/usr/bin/perl

use strict;
use warnings;
use Getopt::Std;

our $filename;
our $opt_o;

END {
	my $status = $?;
	unlink("$opt_o.x.$$")
		if defined($opt_o);
	$? = $status;
}

sub parseErr {
	my $msg = shift;
	
	print(STDERR "$filename, line $.: $msg\n");
	exit 1;
}

getopts("o:");
die "No output file specified"
	if !defined($opt_o);
open(XF, ">", "$opt_o.x.$$") || die "Can't open $opt_o.x.$$";

my %types;
my $type;
# These are the primitive types that can appear as procedure parameters.
# Array types (opaque, string) and unions are not supported here.
foreach $type ("int", "unsigned", "unsigned int", "enum", "bool", "hyper",
			"unsigned hyper", "float", "double", "quadruple",
			"void") {
	$types{$type} = 1;
}

my @procNums = ({}, {});	# First hash is client, second is server
my $inProcDefs = 0;
my $isServerDefs = 0;
my $sym_re = '([a-zA-Z0-9_]+)';
my $type_re = '((unsigned\s+)?[a-zA-Z0-9_]+)';
my $noreply;
my $arg;
my $ret;
my $func;
my $num;
for $filename (@ARGV) {
	open(FH, "<", $filename)
		or die "Can't open $filename";
	while (<FH>) {
		if (!$inProcDefs) {
			if (/^\s*(client|server)procs\s+{/) {
				$inProcDefs = 1;
				$isServerDefs = ($1 eq "server");
				next;
			}
			print XF;
			if (/^\s*(struct|enum)\s+$sym_re\s+{/o) {
				print "Found $1 $2\n";
				$types{$2} = 1;
			}
			if (/^\s*typedef\s+$sym_re\s+$type_re[<\[]?/o) {
				print "Found typedef $2\n";
				$types{$2} = 1;
			}
		} else {
			if (/}/) {
				$inProcDefs = 0;
				next;
			}
			if (/^\s*(noreply)?\s+$sym_re\(($type_re
						(,\s+$type_re)?)?\)\s*=
						\s*([1-9][0-9]*)\s*;/ox) {
				$noreply = defined($1);
				$func = $2;
				$arg = $4;
				$ret = $7;
				$num = $9;
				$arg = "void" if !defined($arg);
				$ret = "void" if !defined($ret);
				parseErr("No such type: $arg")
					if !defined($types{$arg});
				parseErr("No such type: $ret")
					if !defined($types{$ret});
				parseErr("Duplicate procedure number")
					if defined($procNums[$isServerDefs]
								->{$num});
				$procNums[$isServerDefs]->{$num}=1;
				print "noreply " if $noreply;
				print "$func($arg, $ret) = $num\n";
			} elsif (/^\s*$/) {
				next;
			} else {
				parseErr("Invalid syntax");
			}
		}
	}
	close FH;
}

close XF;
rename("$opt_o.x.$$", "$opt_o.x") || die "Couldn't write $opt_o.x";
