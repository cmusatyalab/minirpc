#!/usr/bin/perl

#attributes: is-request
#
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
	
	my @attrlist = ("Response", "Responses", "Initiators");
	my $attr;
	
	foreach $attr (keys %$attrs) {
		grep(/^$attr$/, @attrlist)
			or fail "Unknown attribute specified for $type";
	}
	
	foreach $attr (@attrlist) {
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
for $filename (@ARGV) {
	open(FH, "<", $filename)
		or fail "Can't open $filename";
	while (<FH>) {
		if (/^[\t ]*([a-zA-Z0-9]+)[\t ]+::=/) {
			if (%$attrs) {
				validateHash($attrs, $1);
				$types{$1} = $attrs;
				$attrs = {};
			}
		}
		next if !/^[\t ]*--[\t ]+([a-zA-Z]+)[\t ]*:[\t ]+(.+)$/;
		validateAttr($1, $2);
		$attrs->{$1} = $2;
	}
	close FH;
}

# Stage 3: produce output
print <<EOF;
#include "protocol.h"

#define CLIENT 0x1
#define SERVER 0x2

struct validate_entry {
	asn_TYPE_descriptor_t *type;
	int multi;
	unsigned initiators;
	asn_TYPE_descriptor_t **response_types;
};

EOF

my $responses;
my $type;
my $rtype;
while (($type, $attrs) = each %types) {
	undef $responses;
	foreach $rtype (split(/[\t ]+/, $attrs->{"Responses"})) {
		$responses .= "&asn_DEF_$rtype, ";
	}
	print "asn_TYPE_descriptor_t *${type}_responses[] =\n";
	print "{${responses}NULL};\n\n";
}

my $multi;
my $initiator;
my $initiators;
print "struct validate_entry validate_entries[] = {\n";
while (($type, $attrs) = each %types) {
	$multi = ($attrs->{"Response"} eq "multi");
	$initiators = "0";
	foreach $initiator (split(/[\t ]+/, $attrs->{"Initiators"})) {
		$initiators .= "|" . uc $initiator;
	}
	print "{&asn_DEF_$type, $multi, $initiators, ${type}_responses},\n"
}
print "{NULL, 0, 0, NULL}\n};\n";
