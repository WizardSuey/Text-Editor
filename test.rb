class Car

    attr_accessor :color, :wheels, :doors, :engine

    def initialize(color, wheels, doors, engine)
        @color = color
        @wheels = wheels
        @doors = doors
        @engine = engine
    end

    def color()
        return @color
    end

    def color=(color)
        @color = color
    end
    
    def wheels()
      return @wheels
    end

    def wheels=(wheels)
      @wheels = wheels
    end

end

car = Car.new("red", 4, 4, "Engine3000")
car.color = "blue"

$stdout << "The car's color is " << car.color << ". Car is badass. " << "It has a " << car.doors << " doors and " << "Super cool " << car.engine <<  "\n"
$stdout << "The car has a " << car.wheels << " wheels\n"
