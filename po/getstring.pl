#!/usr/bin/perl -w 
use strict;
use warnings;

my $midcheck = 0;
my @mids;
my @mstr;


# by default pass calaos.pot for ARGV[0] - that's the template.
# this script assumes calaos.pot will have only msgstr "" and not multiline msgstr
open my $infile,  $ARGV[0] or die "Could not open $ARGV[0] $!";

# for temp processing
open my $tmpfile, ">", "temp" or die "Could not open temp $!";


sub write_results()
{
	print $tmpfile "msgid ";
	foreach(@mids)
	{
		print $tmpfile "$_\n";
	}
	
	print $tmpfile "msgstr ";
	foreach(@mstr)
	{
		print $tmpfile "$_";
		print $tmpfile "\n";
	}

	
	@mids = (); #clear
	@mstr = (); #clear
}

while( my $line = <$infile>) 
{   
	print "Inside While...\n";
	$line =~ s/^\s+//;  # remove leading whitespace
	$line =~ s/\s+$//; # remove trailing whitespace
	if (length($line) == 0)
	{
		print "Looping1\n";
		next;
	}
		

    # retain comments
	if($line =~ /^#/)
	{
        print $tmpfile $line;
        print $tmpfile "\n";
        print "Looping1\n";
		next;
	}

	#skip msgstr, commit the collected results
	if($line =~ /^msgstr/)
	{
        write_results();
        print "Looping1\n";
		next;
	}

	$line =~ s/^msgid//g;
	push (@mids, $line);

	my $escaped = quotemeta $line;
	my $res = `php translate.php $escaped $ARGV[1] $ARGV[2]`;
	push (@mstr, $res);
}

print ("Finished\n");
close $infile;
close $tmpfile;
