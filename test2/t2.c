int printf ();

int main ()
{
    int i = 0, sum = 0;
    while (i < 100) {
    	printf ("i = %d\n", i);
        sum = sum + i;
        i   = i + 1;
    }

    printf ("sum = %d\n", sum);
}
