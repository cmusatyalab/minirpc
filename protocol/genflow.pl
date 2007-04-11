#!/usr/bin/perl

# Define: {request|parent}
# Replies: Status Foo
# Initiators: {client|server}+

use strict;
use warnings;
use Getopt::Std;

our $filename;
our @alltypes;
our %line;
our $opt_o;

END {
	my $status = $?;
	unlink("$opt_o.c.$$", "$opt_o.h.$$")
		if defined($opt_o);
	$? = $status;
}

sub parseErr {
	my $type = shift;
	my $attr = shift;
	my $msg = shift;
	
	my $filename;
	my $line;
	
	($filename, $line) = @{$line{$type}->{$attr}};
	print(STDERR "$filename, line $line: $msg\n");
	exit 1;
}

sub validateAttr {
	my $type = shift;
	my $attr = shift;
	my $value = shift;
	
	my $rtype;
	my @vl;
	
	if ($attr eq "Define") {
		$value =~ /^request|parent$/
			or parseErr($type, $attr, "Invalid definition");
	}
	if ($attr eq "Replies") {
		foreach $rtype (split(/[\t ]+/, $value)) {
			grep(/^$rtype$/, @alltypes)
				or parseErr($type, $attr, "Invalid type " .
						"reference name $rtype");
		}
	}
	if ($attr eq "Initiators") {
		@vl = split(/[\t ]+/, $value);
		grep(/^client|server$/, @vl) == @vl
			or parseErr($type, $attr, "Invalid initiators");
	}
}

sub validateHash {
	my $type = shift;
	my $attrs = shift;
	
	my @attrlist;
	my $attr;
	
	if (!defined($attrs->{"Define"})) {
		parseErr($type, "decl", "Definition not specified, but other "
					. "attributes are");
	}
	if ($attrs->{"Define"} eq "request") {
		@attrlist = ("Replies", "Initiators");
	} elsif ($attrs->{"Define"} eq "parent") {
		@attrlist = ()
	}
	
	foreach $attr (keys %$attrs) {
		grep(/^$attr$/, ("Define", @attrlist))
			or parseErr($type, $attr, "Invalid attribute");
	}
	
	foreach $attr ("Define", @attrlist) {
		parseErr($type, "decl", "$attr attribute not specified")
			if (!defined($attrs->{$attr}));
		validateAttr($type, $attr, $attrs->{$attr});
	}
}

sub setline {
	my $type = shift;
	my $attr = shift;
	
	if (!defined($line{$type})) {
		if ($attr eq "decl") {
			# For request definitions, we get attributes before
			# we get the name of the type.  So we store attribute
			# line numbers under the "tmp" type and move the
			# hashref once we know the type name.
			$line{$type} = $line{"tmp"};
			$line{"tmp"} = {};
		} else {
			$line{$type} = {};
		}
	}
	$line{$type}->{$attr} = [$filename, $.];
}

getopts("o:");
die "No output file specified"
	if !defined($opt_o);

# Stage 1: read input files and parse types and attributes
my $attrs = {};
my %types;
my %parentmap;
my $parentName = undef;
my $inParent = 0;
for $filename (@ARGV) {
	open(FH, "<", $filename)
		or die "Can't open $filename";
	while (<FH>) {
		if (/^[\t ]*([a-zA-Z0-9]+)[\t ]+::=/) {
			# Type reference name definition
			setline($1, "decl");
			push @alltypes, $1;
			if (%$attrs) {
				if (defined($attrs->{"Define"}) and
						$attrs->{"Define"} eq
						"parent") {
					if ($parentName) {
						parseErr($1, "Define",
							"There can only be " .
							"one message parent " .
							"(\"$parentName\" " .
							"already chosen)");
					} else {
						$parentName = $1;
						$inParent = 1;
					}
				}
				$types{$1} = $attrs;
				$attrs = {};
			}
			next;
		}
		if (/^[\t ]*--[\t ]+([a-zA-Z]+)[\t ]*:[\t ]+(.+)$/) {
			# Attribute definition
			setline("tmp", $1);
			parseErr("tmp", $1, "Redefinition of attribute $1")
				if (defined($attrs->{$1}));
			$attrs->{$1} = $2;
		}
		if ($inParent) {
			if (/SEQUENCE|SET|CHOICE|{/) {
				parseErr($parentName, "decl", "Nested " .
						"definitions not supported " .
						"in message parent");
			}
			if (/^[\t ]*([a-zA-Z0-9-]+)[\t ]+([a-zA-Z0-9-]+),/) {
				# Choice member
				setline($parentName, $1);
				parseErr($parentName, $1, "Multiple fields " .
						"with the same message type " .
						"in message parent")
					if defined($parentmap{$2});
				$parentmap{$2} = $1;
			}
			if (/}/) {
				# End of parent choice
				$inParent = 0;
			}
		}
	}
	close FH;
}

# Stage 2: validate input
my $type;

die "No message parent found"
	if !$parentName;
while (($type, $attrs) = each %types) {
	validateHash($type, $attrs);
}

# Stage 3: produce output
my $outfile_hdr_define = uc "${opt_o}_H";

open(CF, ">", "$opt_o.c.$$") || die "Can't open $opt_o.c.$$";
open(HF, ">", "$opt_o.h.$$") || die "Can't open $opt_o.h.$$";
print CF "/* AUTOGENERATED FILE -- DO NOT EDIT */\n\n";
print CF "#include \"protocol.h\"\n";
print CF "#include \"$opt_o.h\"\n\n";
print HF <<EOF;
/* AUTOGENERATED FILE -- DO NOT EDIT */

#ifndef $outfile_hdr_define
#define $outfile_hdr_define

#define INITIATOR_CLIENT 0x1
#define INITIATOR_SERVER 0x2

struct flow_params {
	unsigned initiators;
	int nr_reply_types;
	int reply_types[];
};

EOF

my $initiator;
my $initiators;
my $replies;
my $nr_replies;
my $rtype;
my $typevar;

# Produce flow_params structure declarations
while (($type, $attrs) = each %types) {
	next if $attrs->{"Define"} ne "request";
	parseErr($type, "decl", "Request $type not defined in message parent")
		if !defined($parentmap{$type});
	$replies = "";
	$nr_replies = 0;
	foreach $rtype (split(/[\t ]+/, $attrs->{"Replies"})) {
		if (defined($parentmap{$rtype})) {
			$typevar = $parentmap{$rtype};
			$replies .= ", "
				if $replies;
			$replies .= "${parentName}_PR_$typevar";
			$nr_replies++;
		} else {
			parseErr($type, "Replies", "Reply $rtype not defined" .
					" in message parent");
		}
	}
	$initiators = undef;
	foreach $initiator (split(/[\t ]+/, $attrs->{"Initiators"})) {
		$initiators .= "|"
			if $initiators;
		$initiators .= uc "INITIATOR_$initiator";
	}
	$initiators = "0"
		if !$initiators;
	print CF "static const struct flow_params flow_$type =\n";
	print CF "{$initiators, $nr_replies, {${replies}}};\n\n";
}

# Produce *_get_flow() functions
print HF "const struct flow_params *${parentName}_get_flow";
print HF "(enum ${parentName}_PR key);\n";
print CF "const struct flow_params *${parentName}_get_flow";
print CF "(enum ${parentName}_PR key)\n{\n";
print CF "\tswitch (key) {\n";
while (($type, $typevar) = each %parentmap) {
	next if (!defined($types{$type}) or
				$types{$type}->{"Define"} ne "request");
	print CF "\tcase ${parentName}_PR_$typevar:\n";
	print CF "\t\treturn &flow_$type;\n";
}
print CF "\tdefault:\n";
print CF "\t\treturn NULL;\n";
print CF "\t}\n}\n\n";

print HF "\n#endif\n";
close CF;
close HF;
rename("$opt_o.c.$$", "$opt_o.c") || die "Couldn't write $opt_o.c";
rename("$opt_o.h.$$", "$opt_o.h") || die "Couldn't write $opt_o.h";
