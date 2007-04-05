#!/usr/bin/perl

#attributes: is-request
#
# Define: {request|choice}
# Response: {single|multi}
# Responses: Status Foo
# Initiators: {client|server}+

use strict;
use warnings;
use Getopt::Std;

our $filename;
our @alltypes;
our $opt_o;

END {
    my $status=$?;
    unlink("$opt_o.c.$$", "$opt_o.h.$$");
    $? = $status;
}

sub fail {
	print(STDERR "$filename, line $.: ", @_, "\n");
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

getopts("o:");

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
my $outfile_hdr_define = uc "${opt_o}_H";

open(CF, ">", "${opt_o}.c.$$") || die "Can't open ${opt_o}.c.$$";
open(HF, ">", "${opt_o}.h.$$") || die "Can't open ${opt_o}.h.$$";
print CF "#include \"protocol.h\"\n";
print CF "#include \"${opt_o}.h\"\n\n";
print HF <<EOF;
#ifndef $outfile_hdr_define
#define $outfile_hdr_define

#define CLIENT 0x1
#define SERVER 0x2

struct flow_params {
	int multi;
	unsigned initiators;
	int response_types[];
};

EOF

my $type;
my $multi;
my $initiator;
my $initiators;
my $responses;
my $rtype;
my $emitted;
my $typevar;
while (($type, $attrs) = each %types) {
	next if $attrs->{"Define"} ne "request";
	$multi = ($attrs->{"Response"} eq "multi") ? "1" : "0";
	$initiators = "0";
	foreach $initiator (split(/[\t ]+/, $attrs->{"Initiators"})) {
		$initiators .= "|" . uc $initiator;
	}
	$responses = "";
	foreach $rtype (split(/[\t ]+/, $attrs->{"Responses"})) {
		$emitted = 0;
		while (($choiceName, $curchoicemap) = each %choicemap) {
			if (defined($curchoicemap->{$rtype})) {
				$typevar = $curchoicemap->{$rtype};
				$responses .= "${choiceName}_PR_$typevar, ";
				$emitted = 1;
			}
		}
		# XXX need to track line number
		fail "No CHOICE representation available for response $rtype"
			if (!$emitted);
	}
	print CF "static const struct flow_params flow_$type =\n";
	print CF "{$multi, $initiators, {${responses}0}};\n\n";
}

while (($choiceName, $curchoicemap) = each %choicemap) {
	print HF "const struct flow_params *${choiceName}_get_flow";
	print HF "(enum ${choiceName}_PR key);\n";
	print CF "const struct flow_params *${choiceName}_get_flow";
	print CF "(enum ${choiceName}_PR key)\n{\n";
	print CF "\tswitch (key) {\n";
	while (($type, $typevar) = each %$curchoicemap) {
		next if !defined($types{$type});
		$attrs = $types{$type};
		print CF "\tcase ${choiceName}_PR_$typevar:\n";
		print CF "\t\treturn &flow_$type;\n";
	}
	print CF "\tdefault:\n";
	print CF "\t\treturn NULL;\n";
	print CF "\t}\n}\n\n";
}

print HF "\n#endif\n";
close CF;
close HF;
rename("${opt_o}.c.$$", "${opt_o}.c") || die "Couldn't overwrite ${opt_o}.c";
rename("${opt_o}.h.$$", "${opt_o}.h") || die "Couldn't overwrite ${opt_o}.h";
