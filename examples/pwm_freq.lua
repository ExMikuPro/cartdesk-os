pwm.setup(1, 1000)
pwm.write(1, 128)

delay.ms(300)

pwm.setFreq(1, 2000)
delay.ms(300)

pwm.setFreq(1, 4000)
delay.ms(300)

pwm.stop(1)
