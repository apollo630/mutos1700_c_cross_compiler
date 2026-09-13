/*
 * 10_integ/03_linklist.c
 *
 * A self-referential struct (singly linked list), dynamic allocation
 * via malloc(), and list traversal.  "char *malloc()" must be
 * declared before use (K&R rule for pointer-returning library
 * functions).
 */
char *malloc();

struct node {
	int val;
	struct node *next;
};

main()
{
	struct node *head, *cur;
	int i, sum;

	head = 0;
	for (i = 1; i <= 5; i = i + 1) {
		cur = (struct node *) malloc(sizeof(struct node));
		cur->val = i;
		cur->next = head;
		head = cur;
	}

	sum = 0;
	cur = head;
	while (cur != 0) {
		sum = sum + cur->val;
		cur = cur->next;
	}
	return sum;
}
