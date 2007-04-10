#!/usr/bin/perl

# Define: {request|choice}
# Replies: Status Foo
# Multireply: {yes|no}
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
		$value =~ /^request|choice$/
			or parseErr($type, $attr, "Invalid definition");
	}
	if ($attr eq "Replies") {
		foreach $rtype (split(/[\t ]+/, $value)) {
			grep(/^$rtype$/, @alltypes)
				or parseErr($type, $attr, "Invalid type " .
						"reference name $rtype");
		}
	}
	if ($attr eq "Multireply") {
		$value =~ /^no|yes$/
			or parseErr($type, $attr, "Invalid multireply setting");
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
		@attrlist = ("Replies", "Multireply", "Initiators");
	} elsif ($attrs->{"Define"} eq "choice") {
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
my %choicemap;
my $curchoicemap = ();
my $choiceName = undef;
for $filename (@ARGV) {
	open(FH, "<", $filename)
		or die "Can't open $filename";
	while (<FH>) {
		if (/^[\t ]*([a-zA-Z0-9]+)[\t ]+::=/) {
			# Type reference name definition
			setline($1, "decl");
			push @alltypes, $1;
			if (%$attrs) {
				$choiceName = $1
					if (defined($attrs->{"Define"}) and
						$attrs->{"Define"} eq "choice");
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
		if ($choiceName) {
			if (/SEQUENCE|SET|CHOICE|{/) {
				parseErr($choiceName, "decl", "Nested " .
						"definitions not supported " .
						"in choices with \"Define: " .
						"choice\" set");
			}
			if (/^[\t ]*([a-zA-Z0-9-]+)[\t ]+([a-zA-Z0-9-]+),/) {
				# Choice member
				setline($choiceName, $1);
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

# Stage 2: validate input
my $type;
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
	int multi;
	unsigned initiators;
	int nr_reply_types;
	int reply_types[];
};

EOF

my $multi;
my $initiator;
my $initiators;
my $replies;
my $nr_replies;
my $rtype;
my $emitted;
my $typevar;

# Produce flow_params structure declarations
while (($type, $attrs) = each %types) {
	next if $attrs->{"Define"} ne "request";
	$replies = "";
	$nr_replies = 0;
	foreach $rtype (split(/[\t ]+/, $attrs->{"Replies"})) {
		$emitted = 0;
		while (($choiceName, $curchoicemap) = each %choicemap) {
			if (defined($curchoicemap->{$rtype})) {
				$typevar = $curchoicemap->{$rtype};
				$replies .= ", "
					if $replies;
				$replies .= "${choiceName}_PR_$typevar";
				$emitted = 1;
				$nr_replies++;
			}
		}
		if (!$emitted) {
			parseErr($type, "Replies", "No CHOICE " .
					"representation available for reply " .
					$rtype);
		}
	}
	$multi = ($attrs->{"Multireply"} eq "yes") ? "1" : "0";
	$initiators = undef;
	foreach $initiator (split(/[\t ]+/, $attrs->{"Initiators"})) {
		$initiators .= "|"
			if $initiators;
		$initiators .= uc "INITIATOR_$initiator";
	}
	$initiators = "0"
		if !$initiators;
	print CF "static const struct flow_params flow_$type =\n";
	print CF "{$multi, $initiators, $nr_replies, {${replies}}};\n\n";
}

# Produce *_get_flow() functions
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
rename("$opt_o.c.$$", "$opt_o.c") || die "Couldn't write $opt_o.c";
rename("$opt_o.h.$$", "$opt_o.h") || die "Couldn't write $opt_o.h";
