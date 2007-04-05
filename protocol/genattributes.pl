#!/usr/bin/perl

#attributes: is-request
#
# Define: {request|choice}
# Response: {single|multi}
# Responses: Status Foo
# Initiators: {client|server}+

use strict;
use warnings;

our $filename;
our @alltypes;

sub fail {
	print("$filename, line $.: ", @_, "\n");
	exit 1;
}

sub validateAttr {
	my $attr = shift;
	my $value = shift;
	
	my $type;
	my @vl;
	
	if ($attr eq "Define") {
		$value =~ /^request|choice$/
			or fail "Invalid definition"
	}
	if ($attr eq "Response") {
		$value =~ /^single|multi$/
			or fail "Invalid response type";
	}
	if ($attr eq "Responses") {
		foreach $type (split(/[\t ]+/, $value)) {
			grep(/^$type$/, @alltypes)
				or fail "Invalid type reference name $type";
		}
	}
	if ($attr eq "Initiators") {
		@vl = split(/[\t ]+/, $value);
		grep(/^client|server$/, @vl) == @vl
			or fail "Invalid initiators";
	}
}

sub validateHash {
	my $attrs = shift;
	my $type = shift;
	
	my @attrlist;
	my $attr;
	
	fail "Definition not specified, but other attributes are"
		if (!defined($attrs->{"Define"}));
	if ($attrs->{"Define"} eq "request") {
		@attrlist = ("Response", "Responses", "Initiators");
	} elsif ($attrs->{"Define"} eq "choice") {
		@attrlist = ()
	}
	
	foreach $attr (keys %$attrs) {
		grep(/^$attr$/, ("Define", @attrlist))
			or fail "Invalid attribute specified for $type";
	}
	
	foreach $attr ("Define", @attrlist) {
		fail "$attr attribute not specified for $type"
			if (!defined($attrs->{$attr}));
	}
}

# Stage 1: find all type reference names
for $filename (@ARGV) {
	open(FH, "<", $filename)
		or fail "Can't open $filename";
	while (<FH>) {
		push @alltypes, $1
			if (/^[\t ]*([a-zA-Z0-9]+)[\t ]+::=/);
	}
	close FH;
}

# Stage 2: parse and validate attributes for types that have them
my $attrs = {};
my %types;
my %choicemap;
my $curchoicemap = ();
my $choiceName = undef;
for $filename (@ARGV) {
	open(FH, "<", $filename)
		or fail "Can't open $filename";
	while (<FH>) {
		if (/^[\t ]*([a-zA-Z0-9]+)[\t ]+::=/) {
			# Type reference name definition
			if (%$attrs) {
				validateHash($attrs, $1);
				$choiceName = $1
					if $attrs->{"Define"} eq "choice";
				$types{$1} = $attrs;
				$attrs = {};
			}
			next;
		}
		if (/^[\t ]*--[\t ]+([a-zA-Z]+)[\t ]*:[\t ]+(.+)$/) {
			# Attribute definition
			validateAttr($1, $2);
			$attrs->{$1} = $2;
		}
		if ($choiceName) {
			if (/SEQUENCE|SET|CHOICE|{/) {
				fail "Nested definitions not supported " .
						"in choices with \"Define: " .
						"choice\" set"
			}
			if (/^[\t ]*([a-zA-Z0-9-]+)[\t ]+([a-zA-Z0-9-]+),/) {
				# Choice member
				$curchoicemap->{$2} = $1;
			}
			if (/}/) {
				# End of choice
				$choicemap{$choiceName} = $curchoicemap;
				$curchoicemap = ();
				$choiceName = undef;
			}
		}
	}
	close FH;
}

# Stage 3: produce output
print <<EOF;
#include "protocol.h"

#define CLIENT 0x1
#define SERVER 0x2

struct validate_entry {
	int type;
	int multi;
	unsigned initiators;
	int response_types[];
};

EOF

my $responses;
my $type;
my $rtype;
my $typevar;
while (($type, $attrs) = each %types) {
	next if $attrs->{"Define"} ne "request";
	undef $responses;
	foreach $rtype (split(/[\t ]+/, $attrs->{"Responses"})) {
		while (($choiceName, $curchoicemap) = each %choicemap) {
			if (defined($curchoicemap->{$rtype})) {
				$typevar = $curchoicemap->{$rtype};
				$responses .= "${choiceName}_PR_$typevar, "
			}
		}
	}
	print "int ${type}_responses[] =\n";
	print "{${responses}0};\n\n";
}

my $multi;
my $initiator;
my $initiators;
while (($choiceName, $curchoicemap) = each %choicemap) {
	print "struct validate_entry validate_entries_${choiceName}[] = {\n";
	while (($type, $typevar) = each %$curchoicemap) {
		next if !defined($types{$type});
		$attrs = $types{$type};
		$multi = ($attrs->{"Response"} eq "multi");
		$initiators = "0";
		foreach $initiator (split(/[\t ]+/, $attrs->{"Initiators"})) {
			$initiators .= "|" . uc $initiator;
		}
		print "{${choiceName}_PR_$typevar, $multi, $initiators, ${type}_responses},\n"
	}
	print "{0, 0, 0, NULL}\n};\n";
}
