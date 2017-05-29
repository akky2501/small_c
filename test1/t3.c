int printf ();

int fact (int n)
{
    if (n < 0 || n == 0)
        return 1;
    else
        return n * fact (n - 1);
}

int main ()
{
    printf ("fact (%d) = %d\n", 10, fact (10));
}
