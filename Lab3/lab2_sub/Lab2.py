print("ET0735 (DevOps for AIoT) - Lab 2 - Introduction to Python")

def display_main_menu():
    print("Enter numbers separated by commas ")

def get_user_input():
    input_str = input()
    split_list = input_str.split(",")
    # Converts strings to floats
    float_list = [float(i) for i in split_list]
    return float_list

def calc_average(numbers):
    avg = sum(numbers) / len(numbers)
    print("Average: " + str(avg))
    return float(avg)

def find_min_max(numbers):
    min_temp = min(numbers)
    max_temp = max(numbers)
    print("Min temp: " + str(min_temp))
    print("Max temp: " + str(max_temp))
    return [float(min_temp), float(max_temp)]

def sort_temperature(numbers):
    sorted_list = sorted(numbers)
    print("Sorted list: " + str(sorted_list))
    return sorted_list

def calc_median_temperature(numbers):
    sorted_list = sorted(numbers)
    n = len(sorted_list)
    mid = n // 2

    if n % 2 == 0:
        median = (sorted_list[mid - 1] + sorted_list[mid]) / 2
    else:
        median = sorted_list[mid]

    print("Median: " + str(median))
    return float(median)

def main():
    display_main_menu()
    numbers = get_user_input()
    calc_average(numbers)
    find_min_max(numbers)
    sort_temperature(numbers)
    calc_median_temperature(numbers)

if __name__ == "__main__":
    main()
