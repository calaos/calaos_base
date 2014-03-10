#!/usr/bin/perl -w 
use strict;
use warnings;

my $hackcheck = 0;
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
	$line =~ s/^\s+//;  # remove leading whitespace
	$line =~ s/\s+$//; # remove trailing whitespace
	if (length($line) == 0)
	{
		next;
	}
	
	if($hackcheck == 0)
	{
		if($line =~ /^#: src/ )
		{
			$hackcheck = 1;
		}
		else
		{
        	print $tmpfile $line;
        	print $tmpfile "\n";		
		    next;
	    }
	}

    # retain comments
	if($line =~ /^#/)
	{
        print $tmpfile $line;
        print $tmpfile "\n";
		next;
	}

	#skip msgstr, commit the collected results
	if($line =~ /^msgstr/)
	{
        write_results();
		next;
	}

	$line =~ s/^msgid//g;
	push (@mids, $line);

	# escape the text
	my $escaped = quotemeta $line;
	# translate
	my $res = `php translate.php $escaped $ARGV[1] $ARGV[2]`;
	# stock
	push (@mstr, $res);
}

print ("Finished\n");
close $infile;
close $tmpfile;
