((lambda ()
   (println "hello world")
   (println (+ 4 2))

   (((lambda (b)
	   ((lambda (f)
		  (b (lambda (x) ((f f) x))))
		(lambda (f)
		  (b (lambda (x) ((f f) x))))))
	 (lambda (f)
       (lambda (n)
         (println n)
         (f (+ n 1)))))
    0)))
