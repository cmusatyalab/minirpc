#!/usr/bin/perl

use strict;
use warnings;
use Getopt::Std;
use File::Compare;
use Text::Wrap;

our $opt_o;
our %outfiles;
our %types;

END {
	my $status = $?;
	my $file;
	foreach $file (values %outfiles) {
		unlink("$file.$$");
	}
	# Special case: temporary file which is never promoted to a real
	# output file
	unlink("${opt_o}.x.$$")
		if $opt_o;
	$? = $status;
}

sub parseErr {
	my $file = shift;
	my $line = shift;
	my $msg = shift;
	
	print(STDERR "$file, line $line: $msg\n");
	exit 1;
}

sub openFile {
	my $handle = shift;
	my $name = shift;
	
	open($handle, ">", "$name.$$") || die "Can't open $name.$$";
	$outfiles{$handle} = $name;
	print $handle "/* AUTOGENERATED FILE -- DO NOT EDIT */\n";
}

sub closeFiles {
	my $handle;
	my $file;
	
	while (($handle, $file) = each %outfiles) {
		close($handle);
		if (!compare("$file.$$", $file)) {
			unlink("$file.$$");
		} else {
			rename("$file.$$", $file) || die "Couldn't write $file";
		}
		delete $outfiles{$handle};
	}
}

sub wrapc {
	my $input = shift;
	
	my $output;
	my $line;
	
	local($Text::Wrap::columns) = 80;
	local($Text::Wrap::huge) = "overflow";
	# Only break at argument boundaries
	local($Text::Wrap::break) = qr/, /;
	# ...and re-add the comma afterward.  Newer versions of Text::Wrap use
	# separator2 for newly-added line breaks; older versions just use
	# separator.  Test is formatted strangely since local() declaration
	# can't be made inside a block
	local($Text::Wrap::separator2) = ",\n"
		if defined($Text::Wrap::separator2);
	local($Text::Wrap::separator) = ",\n"
		if !defined($Text::Wrap::separator2);

	foreach $line (split(/\n/, $input)) {
		$output .= Text::Wrap::wrap("", "\t\t\t", $line) . "\n";
	}
	return $output;
}

sub argument {
	my $type = shift;
	my $var = shift;
	
	if ($type ne "void") {
		return ", $type $var";
	} else {
		return "";
	}
}

sub parameter {
	my $type = shift;
	my $param = shift;
	
	if ($type ne "void") {
		return $param;
	} else {
		return "NULL";
	}
}

sub typesize {
	my $type = shift;
	
	if ($type ne "void") {
		return "sizeof($type)";
	} else {
		return "0";
	}
}

# Sort hash keys numerically: 0..MAX, -1..MIN
sub opcodeSort {
	my $hash = shift;
	
	my @nums;
	
	@nums = sort {$a <=> $b} grep ($_ >= 0, keys %$hash);
	@nums = (@nums, sort {$b <=> $a} grep ($_ < 0, keys %$hash));
	return @nums;
}

sub gen_sender_stub_c {
	my $fh = shift;
	my $func = shift;
	my $in = shift;
	my $out = shift;
	
	my $inarg = argument($in, "*in");
	my $outarg = argument($out, "**out");
	my $inparam = parameter($in, "in");
	my $outparam = parameter($out, "out");
	
	print $fh wrapc(<<EOF);

int $func(struct mrpc_connection *conn$inarg$outarg)
{
	return mrpc_send_request(conn, nr_$func, $inparam, $outparam);
}

int ${func}_async(struct mrpc_connection *conn, ${func}_callback_fn *callback, void *private$inarg)
{
	return mrpc_send_request_async(conn, nr_$func, $inparam, callback, private);
}
EOF
}

sub gen_sender_stub_sync_h {
	my $fh = shift;
	my $func = shift;
	my $in = shift;
	my $out = shift;
	
	my $inarg = argument($in, "*in");
	my $outarg = argument($out, "**out");
	
	print $fh wrapc(<<EOF);
int $func(struct mrpc_connection *conn$inarg$outarg);
EOF
}

sub gen_sender_stub_typedef_h {
	my $fh = shift;
	my $func = shift;
	my $in = shift;
	my $out = shift;
	
	my $outarg = argument($out, "*reply");
	
	print $fh wrapc(<<EOF);
typedef void (${func}_callback_fn)(void *conn_private, void *msg_private, int status$outarg);
EOF
}

sub gen_sender_stub_async_h {
	my $fh = shift;
	my $func = shift;
	my $in = shift;
	my $out = shift;
	
	my $inarg = argument($in, "*in");
	
	print $fh wrapc(<<EOF);
int ${func}_async(struct mrpc_connection *conn, ${func}_callback_fn *callback, void *private$inarg);
EOF
}

sub gen_receiver_stub_c {
	my $fh = shift;
	my $func = shift;
	my $in = shift;
	my $out = shift;
	
	my $outarg = argument($out, "*out");
	my $outparam = parameter($out, "out");
	
	print $fh wrapc(<<EOF);

int ${func}_send_async_reply(struct mrpc_message *request, int status$outarg)
{
	return mrpc_send_reply(request, status, $outparam);
}
EOF
}

sub gen_receiver_stub_h {
	my $fh = shift;
	my $func = shift;
	my $in = shift;
	my $out = shift;
	
	my $outarg = argument($out, "*out");
	
	print $fh wrapc(<<EOF);
int ${func}_send_async_reply(struct mrpc_message *request, int status$outarg);
EOF
}

sub gen_oneway_stub_c {
	my $fh = shift;
	my $func = shift;
	my $in = shift;
	my $out = shift;
	
	my $inarg = argument($in, "*in");
	my $inparam = parameter($in, "in");
	
	print $fh wrapc(<<EOF);

int ${func}(struct mrpc_connection *conn$inarg)
{
	return mrpc_send_request_noreply(conn, nr_$func, $inparam);
}
EOF
}

sub gen_oneway_stub_h {
	my $fh = shift;
	my $func = shift;
	my $in = shift;
	my $out = shift;
	
	my $inarg = argument($in, "*in");
	
	print $fh wrapc(<<EOF);
int ${func}(struct mrpc_connection *conn$inarg);
EOF
}

sub gen_info_proc {
	my $fh = shift;
	my $role = shift;
	my $isReply = shift;
	my $procs = shift;
	
	my $reply = $isReply ? "reply" : "request";
	my $num;
	my $func;
	my $type;
	my $typesize;
	
	print $fh wrapc(<<EOF);

static int ${opt_o}_${role}_${reply}_info(unsigned cmd, xdrproc_t *type, unsigned *size)
{
	switch (cmd) {
EOF
	
	foreach $num (opcodeSort($procs)) {
		$func = @{$procs->{$num}}[2];
		$type = @{$procs->{$num}}[$isReply ? 4 : 3];
		$typesize = typesize($type);
		print $fh wrapc(<<EOF);
	case nr_$func:
		SET_PTR_IF_NOT_NULL(type, (xdrproc_t)xdr_$type);
		SET_PTR_IF_NOT_NULL(size, $typesize);
		return MINIRPC_OK;
EOF
	}
	
	print $fh wrapc(<<EOF);
	default:
		return MINIRPC_PROCEDURE_UNAVAIL;
	}
}
EOF
}

sub gen_opcode_enum {
	my $fh = shift;
	my $role = shift;
	my $procs = shift;
	
	my $num;
	my $func;
	
	return if (keys %$procs == 0);
	print $fh "\nenum ${opt_o}_${role}_procedures {\n";
	foreach $num (opcodeSort($procs)) {
		$func = @{$procs->{$num}}[2];
		print $fh "\tnr_$func = $num,\n";
	}
	print $fh "};\n";
}

sub gen_operations_struct {
	my $fh = shift;
	my $role = shift;
	my $procs = shift;
	
	my $num;
	my $func;
	my $in;
	my $out;
	my $inarg;
	my $outarg;
	
	return if (keys %$procs == 0);
	print $fh "\nstruct ${opt_o}_${role}_operations {\n";
	foreach $num (opcodeSort($procs)) {
		($func, $in, $out) = @{$procs->{$num}}[2..4];
		$inarg = argument($in, "*in");
		$outarg = argument($out, "*out");
		print $fh wrapc("\tint (*$func)(void *conn_data, struct mrpc_message *msg$inarg$outarg);");
	}
	print $fh "};\n";
}

sub genstubs {
	my $role = shift;
	my $procs = shift;
	my $sfh = shift;
	my $rfh = shift;
	my $mfh = shift;
	
	my @keys;
	my $file;
	my $line;
	my $num;
	my $func;
	my $arg;
	my $ret;
	my $fh;
	
	# validation
	foreach $num (opcodeSort($procs)) {
		($file, $line, $func, $arg, $ret) = @{$procs->{$num}};
		parseErr($file, $line, "No such type: $arg")
			if !defined($types{$arg});
		parseErr($file, $line, "No such type: $ret")
			if !defined($types{$ret});
		parseErr($file, $line, "Procedures in ${role}msgs " .
					"section cannot return a value")
			if $num < 0 && $ret ne "void";
		print "$func($arg, $ret) = $num\n";
	}
	
	# toplevel stuff
	gen_opcode_enum($mfh->[1], $role, $procs);
	gen_info_proc($mfh->[0], $role, 0, $procs);
	gen_info_proc($mfh->[0], $role, 1, $procs);	
	gen_operations_struct($rfh->[1], $role, $procs);
	
	# request-reply
	@keys = sort {$a <=> $b} grep ($_ >= 0, keys %$procs);
	print {$sfh->[1]} "\n";
	print {$rfh->[1]} "\n";
	foreach $num (@keys) {
		($file, $line, $func, $arg, $ret) = @{$procs->{$num}};
		gen_sender_stub_c($sfh->[0], $func, $arg, $ret);
		gen_receiver_stub_c($rfh->[0], $func, $arg, $ret);
		gen_sender_stub_typedef_h($sfh->[1], $func, $arg, $ret);
		gen_receiver_stub_h($rfh->[1], $func, $arg, $ret);
	}
	print {$sfh->[1]} "\n";
	foreach $num (@keys) {
		($file, $line, $func, $arg, $ret) = @{$procs->{$num}};
		gen_sender_stub_sync_h($sfh->[1], $func, $arg, $ret);
	}
	print {$sfh->[1]} "\n";
	foreach $num (@keys) {
		($file, $line, $func, $arg, $ret) = @{$procs->{$num}};
		gen_sender_stub_async_h($sfh->[1], $func, $arg, $ret);
	}
	
	# noreply
	@keys = sort {$b <=> $a} grep ($_ < 0, keys %$procs);
	foreach $num (@keys) {
		($file, $line, $func, $arg, $ret) = @{$procs->{$num}};
		gen_oneway_stub_c($sfh->[0], $func, $arg, $ret);
		gen_oneway_stub_h($sfh->[1], $func, $arg, $ret);
	}
}

getopts("o:");
if (!defined($opt_o) || @ARGV == 0) {
	print "Usage: $0 -o <output_file_base_name> <input_files>\n";
	exit 1;
}

# Initialize primitive types
# These are the primitive types that can appear as procedure parameters.
# Right now we only support void, as a special case, because we don't want
# to make assumptions about the native (unserialized) length of the data
# types that XDR produces.  Regular types ("int", "bool", etc.) can still
# be used with XDR typedefs, because sizeof(some_typedef) will do the right
# thing (as opposed to sizeof(hyper), which is the only thing we could do
# since we don't know what C type "hyper" corresponds to).
$types{"void"} = 1;

# Preprocess the input files with cpp and parse them
my $infile;
my $filename;
my $line;
my $data;
my %procs;
my %procNames;
my $curProcData;
my $curDefs;
my $noreply;
my $sym_re = '([a-zA-Z0-9_]+)';
my $type_re = '((unsigned\s+)?[a-zA-Z0-9_]+)';
my $func;
my $num;
for $infile (@ARGV) {
	$data=`cpp $infile`;
	die "Couldn't open $infile"
		if $?;
	$filename = $infile;
	$line = 0;
	foreach $_ (split /\n/, $data) {
		$line++;
		if (/^# ([1-9][0-9]*) "([^"]+)"/) {
			# cpp line marker
			$filename = $2;
			$line = $1 - 1;
		}
		if (!$curDefs) {
			if (/^\s*(client|server)(procs|msgs)\s+{/) {
				$curDefs = $1;
				$noreply = ($2 eq "msgs");
				$procs{$curDefs} = {}
					if !exists($procs{$curDefs});
				next;
			}
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
				undef $curDefs;
				next;
			}
			if (/^\s*$sym_re\(($type_re(,\s+$type_re)?)?\)\s*=
						\s*([1-9][0-9]*)\s*;/ox) {
				$func = $1;
				$num = $8;
				$num = -$num
					if $noreply;
				# file, line, func, arg, ret
				$curProcData = [$filename, $line, $func,
							$3 ? $3 : "void",
							$6 ? $6 : "void"];
				parseErr($filename, $line, "Duplicate " .
							"procedure number")
					if defined($procs{$curDefs}->{$num});
				parseErr($filename, $line, "Duplicate " .
							"procedure name")
					if defined($procNames{$func});
				$procs{$curDefs}->{$num} = $curProcData;
				$procNames{$func} = 1;
			} elsif (/^\s*$/) {
				next;
			} else {
				parseErr($filename, $line, "Invalid syntax");
			}
		}
	}
}

# Generate stubs
my @cfh;
my @sfh;
my @mfh;
openFile(*CCF, "${opt_o}_client.c");
openFile(*CHF, "${opt_o}_client.h");
openFile(*SCF, "${opt_o}_server.c");
openFile(*SHF, "${opt_o}_server.h");
openFile(*MCF, "${opt_o}_common.c");
openFile(*MHF, "${opt_o}_common.h");
@cfh = (*CCF, *CHF);
@sfh = (*SCF, *SHF);
@mfh = (*MCF, *MHF);
genstubs("server", $procs{"server"}, \@cfh, \@sfh, \@mfh)
	if defined($procs{"server"});
genstubs("client", $procs{"client"}, \@sfh, \@cfh, \@mfh)
	if defined($procs{"client"});

# Read the input files again, this time without cpp, and generate a .x file
# suitable for parsing with rpcgen.  Try to preserve line numbers.
my $inDefs = 0;
open(XF, ">", "${opt_o}.x.$$") || die "Can't open ${opt_o}.x.$$";
for $infile (@ARGV) {
	open(FH, "<", $infile) || die "Can't open $infile";
	while (<FH>) {
		if (!$inDefs) {
			if (/^\s*(client|server)(procs|msgs)\s+{/) {
				$inDefs = 1;
				print XF "\n";
			} else {
				print XF;
			}
		} else {
			$inDefs = 0
				if (/}/);
			print XF "\n";
		}
	}
}
close(XF);

# Generate xdr.c
open(IF, "-|", "rpcgen -c $opt_o.x.$$") or
	die "Couldn't generate ${opt_o}_xdr.c";
openFile(*XCF, "${opt_o}_xdr.c");
while (<IF>) {
	s/${opt_o}\.x\.h/${opt_o}_xdr.h/
		if /#include/;
	print XCF;
}

# Generate xdr.h
my $olddefine = uc "_$opt_o.x_H_RPCGEN";
my $newdefine = uc "${opt_o}_XDR_H";
open(IF, "-|", "rpcgen -h $opt_o.x.$$") or
	die "Couldn't generate ${opt_o}_xdr.h";
openFile(*XHF, "${opt_o}_xdr.h");
while (<IF>) {
	s/$olddefine/$newdefine/;
	print XHF;
}

# Commit output
closeFiles();
