#!/usr/bin/perl
use strict;
use 5.010;
use Scalar::Util qw(looks_like_number);

no warnings 'experimental::smartmatch';

my($fileIn);
my($fileOut);
my($numRows) = 1;


my($parameter);
my($value);
my($perc);


while(@ARGV)
{
    # ...we'll extract a parameter...
    $parameter = shift @ARGV;

    # ...and its value!
    $value = shift @ARGV;

    # it must be a value!
    if(!defined($value)){
        die "<E> Wrong Format!!!\n";
    }

    # store the value into the correct variable
    given( $parameter ) 
    {
        # parameter file to be opened
        when(/^-file/)
        {
            # assign
            $fileIn = $value;
        }

        when(/^-output/)
        {
            # assign
            $fileOut = $value;
        }

        when(/^-rows/)
        {
            # assign
            $numRows = $value;
        }

        default
        {
            die "<E> Unknown parameter \"$parameter\"! Run --h\n";
        }
    }
}

my($fl) = $fileIn;
my(@samples);
my($data_line);

# open input file
open(DAT, $fl) || die("Could not open file!");
my(@raw_data) = <DAT>;
close(DAT);

my($lineCounter) = 0;
foreach $data_line (@raw_data)
{
    chomp($data_line);

    #skip the first line
    if($lineCounter == 0)
    {
        $lineCounter++;
        next;
    }

    my(@data_values)=split(",",$data_line);
    my $val = $data_values[15];
   
    push @samples, $val;

    $lineCounter++;
}

# --- compute mean --- #
my($count) = 0;
my($sum)=0;
foreach my $sample (@samples)
{
    $sum = $sum + $sample; 
    $count++;
}
my($mean)=$sum/$count;

# --- compute confidence interval --- #
$sum = 0;
$count = 0;
foreach my $sample (@samples)
{
    $sum = $sum + ($sample - $mean)**2;
    $count++;
}
my($stddev) = sqrt($sum/$count);
my($confidence) = 1.96*($stddev/sqrt($count));

# --- print output --- #
print $mean."\t".$confidence;

