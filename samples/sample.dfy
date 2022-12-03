method sum1(in1 : int, in2 : int) 
    returns (output : int) 
    ensures output >= 4
{
    output := in1 + in2;
}

method sum2(in1 : int, in2 : int) 
    returns (output : int) 
    requires in1 > 1 && in2 > 1
    ensures output >= 4
{
    output := in1 + in2;
}




